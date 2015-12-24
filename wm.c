/*
 * http://www.x.org/wiki/UserDocumentation/
 * http://www.x.org/releases/X11R7.7/doc/libX11/libX11/libX11.html
 * http://seasonofcode.com/posts/how-x-window-managers-work-and-how-to-write-one-part-i.html
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>

#include "ipc.h"

enum DecorationLocation
{
    DecTopLeft = 0,    DecTop = 1,    DecTopRight = 2,
    DecLeft = 3,       DecRight = 4,
    DecBottomLeft = 5, DecBottom = 6, DecBottomRight = 7,

    DecLAST = 8
};

struct DecorationGeometry
{
    int top_height;
    int left_width, right_width;
    int bottom_height;
};

enum DecTint
{
    DecTintNormal = 0,
    DecTintSelect = 1,
    DecTintUrgent = 2,

    DecTintLAST = 3
};

#include "pixmaps.h"

struct Client
{
    Window win;

    /* Inner size of the actual client */
    int x, y, w, h;

    struct Monitor *mon;

    Window decwin[DecLAST];
    GC decwin_gc[DecLAST];

    struct Client *next;
};

struct Monitor
{
    /* Actual monitor size */
    int mx, my, mw, mh;

    /* Logical size, i.e. where we can place windows */
    int wx, wy, ww, wh;

    struct Monitor *next;
};

static struct Client *clients = NULL;
static struct Client *mouse_dc = NULL;
static struct Monitor *monitors = NULL, *selmon = NULL;
static int mouse_dx, mouse_dy, mouse_ocx, mouse_ocy, mouse_ocw, mouse_och;
static Display *dpy;
static XImage *dec_ximg[3];
static Window root, command_window;
static int running = 1;
static int screen;
static int (*xerrorxlib)(Display *, XErrorEvent *);

static struct Client *client_get_for_decoration(Window win,
                                                enum DecorationLocation *which);
static struct Client *client_get_for_window(Window win);
static void client_save(struct Client *c);
static void decorations_create(struct Client *c);
static void decorations_destroy(struct Client *c);
static void decorations_load(void);
static char *decorations_tint(unsigned long color);
static XImage *decorations_to_ximg(char *data);
static void handle_clientmessage(XEvent *e);
static void handle_configurerequest(XEvent *e);
static void handle_destroynotify(XEvent *e);
static void handle_enternotify(XEvent *e);
static void handle_expose(XEvent *e);
static void handle_maprequest(XEvent *e);
static void handle_unmapnotify(XEvent *e);
static void ipc_mouse_move(char arg);
static void ipc_mouse_resize(char arg);
static void ipc_noop(char arg);
static void manage(Window win, XWindowAttributes *wa);
static void manage_fit_on_monitor(struct Client *c);
static void manage_raisefocus(struct Client *c);
static void manage_setsize(struct Client *c);
static void run(void);
static void scan(void);
static void setup(void);
static void shutdown(void);
static void unmanage(struct Client *c);
static int xerror(Display *dpy, XErrorEvent *ee);

static void (*ipc_handler[IPCLast]) (char arg) = {
    [IPCMouseMove] = ipc_mouse_move,
    [IPCMouseResize] = ipc_mouse_resize,
    [IPCNoop] = ipc_noop,
};

static void (*x11_handler[LASTEvent]) (XEvent *) = {
    [ClientMessage] = handle_clientmessage,
    [ConfigureRequest] = handle_configurerequest,
    [DestroyNotify] = handle_destroynotify,
    [EnterNotify] = handle_enternotify,
    [Expose] = handle_expose,
    [MapRequest] = handle_maprequest,
    [UnmapNotify] = handle_unmapnotify,
};

struct Client *
client_get_for_decoration(Window win, enum DecorationLocation *which)
{
    struct Client *c;
    size_t i;

    for (c = clients; c; c = c->next)
    {
        for (i = DecTopLeft; i <= DecBottomRight; i++)
        {
            if (c->decwin[i] == win)
            {
                *which = i;
                return c;
            }
        }
    }

    *which = DecLAST;
    return NULL;
}

struct Client *
client_get_for_window(Window win)
{
    struct Client *c;

    for (c = clients; c; c = c->next)
        if (c->win == win)
            return c;

    return NULL;
}

void
client_save(struct Client *c)
{
    c->next = clients;
    clients = c;
}

void
decorations_create(struct Client *c)
{
    size_t i;
    XSetWindowAttributes wa = {
        .override_redirect = True,
        .background_pixmap = ParentRelative,
        .event_mask = ExposureMask,
    };

    for (i = DecTopLeft; i <= DecBottomRight; i++)
    {
        c->decwin[i] = XCreateWindow(
                dpy, root, 0, 0, 10, 10, 0,
                DefaultDepth(dpy, screen),
                CopyFromParent, DefaultVisual(dpy, screen),
                CWOverrideRedirect|CWBackPixmap|CWEventMask,
                &wa
        );
        XMapRaised(dpy, c->decwin[i]);
        c->decwin_gc[i] = XCreateGC(dpy, root, 0, NULL);
        XSetLineAttributes(dpy, c->decwin_gc[i], 1, LineSolid, CapButt,
                           JoinMiter);
    }
}

void
decorations_destroy(struct Client *c)
{
    size_t i;

    for (i = DecTopLeft; i <= DecBottomRight; i++)
    {
        XFreeGC(dpy, c->decwin_gc[i]);
        XUnmapWindow(dpy, c->decwin[i]);
        XDestroyWindow(dpy, c->decwin[i]);
    }
}

void
decorations_load(void)
{
    char *tinted[DecTintLAST];
    size_t i;

    for (i = DecTintNormal; i <= DecTintUrgent; i++)
    {
        tinted[i] = decorations_tint(dec_tints[i]);
        dec_ximg[i] = decorations_to_ximg(tinted[i]);
    }
}

char *
decorations_tint(unsigned long color)
{
    unsigned int r, g, b, tr, tg, tb;
    unsigned int *out;
    size_t i;

    out = (unsigned int *)malloc(sizeof dec_img);
    assert(out != NULL);

    tr = (0x00FF0000 & color) >> 16;
    tg = (0x0000FF00 & color) >> 8;
    tb = (0x000000FF & color);

    for (i = 0; i < sizeof dec_img / sizeof dec_img[0]; i++)
    {
        /* r = original_r * (tint / 256) */

        r = (0x00FF0000 & dec_img[i]) >> 16;
        g = (0x0000FF00 & dec_img[i]) >> 8;
        b = (0x000000FF & dec_img[i]);

        r *= tr;
        g *= tg;
        b *= tb;

        r /= 256;
        g /= 256;
        b /= 256;

        r = r > 255 ? 255 : r;
        g = g > 255 ? 255 : g;
        b = b > 255 ? 255 : b;

        out[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
    }

    return (char *)out;
}

XImage *
decorations_to_ximg(char *data)
{
    return XCreateImage(dpy, DefaultVisual(dpy, screen), 24, ZPixmap, 0,
                        data,
                        dgeo.left_width + 1 + dgeo.right_width,
                        dgeo.top_height + 1 + dgeo.bottom_height,
                        32, 0);
}

void
handle_clientmessage(XEvent *e)
{
    XClientMessageEvent *cme = &e->xclient;
    static Atom t = None;
    enum IPCCommand cmd;
    char arg;

    if (t == None)
        t = XInternAtom(dpy, "_"__NAME_UPPERCASE__"_CLIENT_COMMAND", False);

    if (cme->message_type != t)
    {
        fprintf(stderr, __NAME__": Received client message with unknown type");
        return;
    }

    cmd = (enum IPCCommand)cme->data.b[0];
    arg = (char)cme->data.b[1];

    if (ipc_handler[cmd])
        ipc_handler[cmd](arg);
}

void
handle_configurerequest(XEvent *e)
{
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    XWindowChanges wc;
    struct Client *c = NULL;

    if ((c = client_get_for_window(ev->window)))
    {
        /* This is a known client. Only respond to move/resize and
         * update our local values. */
        if (ev->value_mask & CWX)
            c->x = ev->x;
        if (ev->value_mask & CWY)
            c->y = ev->y;
        if (ev->value_mask & CWWidth)
            c->w = ev->width;
        if (ev->value_mask & CWHeight)
            c->h = ev->height;

        if (ev->value_mask & (CWX | CWY | CWWidth | CWHeight))
            manage_setsize(c);
    }
    else
    {
        /* We don't know anything about this client yet, so we give him
         * just what he wants. */
        wc.x = ev->x;
        wc.y = ev->y;
        wc.width = ev->width;
        wc.height = ev->height;
        wc.border_width = ev->border_width;
        wc.sibling = ev->above;
        wc.stack_mode = ev->detail;
        XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
    }
}

void
handle_destroynotify(XEvent *e)
{
    XDestroyWindowEvent *ev = &e->xdestroywindow;
    struct Client *c;

    if ((c = client_get_for_window(ev->window)))
        unmanage(c);
}

void
handle_enternotify(XEvent *e)
{
    XCrossingEvent *ev = &e->xcrossing;

    /* Taken from dwm */
    if ((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
        return;

    XSetInputFocus(dpy, ev->window, RevertToPointerRoot, CurrentTime);
}

void
handle_expose(XEvent *e)
{
    XExposeEvent *ev = &e->xexpose;
    struct Client *c = NULL;
    enum DecorationLocation which = DecLAST;
    enum DecTint tint = DecTintSelect;
    int x, y;

    if ((c = client_get_for_decoration(ev->window, &which)) == NULL)
        return;

    /* TODO optimize for speed, using tiling */

    switch (which)
    {
        case DecTopLeft:
            XPutImage(dpy, c->decwin[which], c->decwin_gc[which],
                      dec_ximg[tint], 0, 0, 0, 0,
                      dgeo.left_width, dgeo.top_height);
            break;
        case DecTop:
            for (x = 0; x < c->w; x++)
                XPutImage(dpy, c->decwin[which], c->decwin_gc[which],
                          dec_ximg[tint], dgeo.left_width, 0, x, 0,
                          1, dgeo.top_height);
            break;
        case DecTopRight:
            XPutImage(dpy, c->decwin[which], c->decwin_gc[which],
                      dec_ximg[tint], dgeo.left_width + 1, 0, 0, 0,
                      dgeo.right_width, dgeo.top_height);
            break;

        case DecLeft:
            for (y = 0; y < c->h; y++)
                XPutImage(dpy, c->decwin[which], c->decwin_gc[which],
                          dec_ximg[tint], 0, dgeo.top_height, 0, y,
                          dgeo.left_width, 1);
            break;
        case DecRight:
            for (y = 0; y < c->h; y++)
                XPutImage(dpy, c->decwin[which], c->decwin_gc[which],
                          dec_ximg[tint], dgeo.left_width + 1, dgeo.top_height,
                          0, y,
                          dgeo.right_width, 1);
            break;

        case DecBottomLeft:
            XPutImage(dpy, c->decwin[which], c->decwin_gc[which],
                      dec_ximg[tint], 0, dgeo.top_height + 1, 0, 0,
                      dgeo.left_width, dgeo.top_height);
            break;
        case DecBottom:
            for (x = 0; x < c->w; x++)
                XPutImage(dpy, c->decwin[which], c->decwin_gc[which],
                          dec_ximg[tint], dgeo.left_width, dgeo.top_height + 1,
                          x, 0, 1, dgeo.top_height);
            break;
        case DecBottomRight:
            XPutImage(dpy, c->decwin[which], c->decwin_gc[which],
                      dec_ximg[tint], dgeo.left_width + 1, dgeo.top_height + 1,
                      0, 0, dgeo.right_width, dgeo.top_height);
            break;

        case DecLAST:
            /* ignore */
            break;
    }
}

void
handle_maprequest(XEvent *e)
{
    XMapRequestEvent *ev = &e->xmaprequest;
    XWindowAttributes wa;

    if (!XGetWindowAttributes(dpy, ev->window, &wa))
        return;
    if (wa.override_redirect)
        return;

    manage(ev->window, &wa);
}

void
handle_unmapnotify(XEvent *e)
{
    XUnmapEvent *ev = &e->xunmap;
    struct Client *c;

    if ((c = client_get_for_window(ev->window)))
        unmanage(c);
}

void
ipc_mouse_move(char arg)
{
    int x, y, di, dx, dy;
    unsigned int dui;
    Window child, dummy;
    struct Client *c;

	XQueryPointer(dpy, root, &dummy, &child, &x, &y, &di, &di, &dui);

    if (arg == 0)
    {
        fprintf(stderr, __NAME__": Mouse move: down at %d, %d over %lu\n",
                x, y, child);

        mouse_dc = NULL;

        if ((c = client_get_for_window(child)))
        {
            mouse_dc = c;
            mouse_dx = x;
            mouse_dy = y;
            mouse_ocx = c->x;
            mouse_ocy = c->y;
            mouse_ocw = c->w;
            mouse_och = c->h;

            manage_raisefocus(c);
        }
    }
    else if (arg == 1)
    {
        fprintf(stderr, __NAME__": Mouse move: motion to %d, %d\n", x, y);

        if (mouse_dc)
        {
            dx = x - mouse_dx;
            dy = y - mouse_dy;

            mouse_dc->x = mouse_ocx + dx;
            mouse_dc->y = mouse_ocy + dy;

            manage_setsize(mouse_dc);
        }
    }
    else if (arg == 2)
        mouse_dc = NULL;
}

void
ipc_mouse_resize(char arg)
{
    /* TODO lots of code duplication from ipc_mouse_move() */

    int x, y, di, dx, dy;
    unsigned int dui;
    Window child, dummy;
    struct Client *c;

	XQueryPointer(dpy, root, &dummy, &child, &x, &y, &di, &di, &dui);

    if (arg == 0)
    {
        fprintf(stderr, __NAME__": Mouse resize: down at %d, %d over %lu\n",
                x, y, child);

        mouse_dc = NULL;

        if ((c = client_get_for_window(child)))
        {
            mouse_dc = c;
            mouse_dx = x;
            mouse_dy = y;
            mouse_ocx = c->x;
            mouse_ocy = c->y;
            mouse_ocw = c->w;
            mouse_och = c->h;

            manage_raisefocus(c);
        }
    }
    else if (arg == 1)
    {
        fprintf(stderr, __NAME__": Mouse resize: motion to %d, %d\n", x, y);

        if (mouse_dc)
        {
            dx = x - mouse_dx;
            dy = y - mouse_dy;

            mouse_dc->w = mouse_ocw + dx;
            mouse_dc->h = mouse_och + dy;

            manage_setsize(mouse_dc);
        }
    }
    else if (arg == 2)
        mouse_dc = NULL;
}

void
ipc_noop(char arg)
{
    fprintf(stderr, __NAME__": ipc: Noop (arg %d)\n", arg);
}

void
manage(Window win, XWindowAttributes *wa)
{
    struct Client *c;

    if (client_get_for_window(win))
    {
        fprintf(stderr, __NAME__": Window %lu is already known, won't map\n",
                win);
        return;
    }

    c = calloc(1, sizeof (struct Client));

    c->win = win;
    c->mon = selmon;

    c->x = wa->x;
    c->y = wa->y;
    c->w = wa->width;
    c->h = wa->height;

    XSetWindowBorderWidth(dpy, c->win, 0);

    XSelectInput(dpy, c->win, 0
                              /* Focus */
                              | EnterWindowMask
                              );

    XMapWindow(dpy, c->win);

    decorations_create(c);
    manage_fit_on_monitor(c);
    manage_setsize(c);
    manage_raisefocus(c);

    client_save(c);

    fprintf(stderr, __NAME__": Managing window %lu (%p) at %dx%d+%d+%d\n",
            c->win, (void *)c, c->w, c->h, c->x, c->y);
}

void
manage_fit_on_monitor(struct Client *c)
{
    if (c->mon == NULL)
    {
        fprintf(stderr, __NAME__": No monitor assigned to %lu (%p)\n",
                c->win, (void *)c);
        return;
    }

    if (c->x - dgeo.left_width < c->mon->wx)
        c->x = c->mon->wx + dgeo.left_width;
    if (c->y - dgeo.top_height < c->mon->wy)
        c->y = c->mon->wy + dgeo.top_height;
    if (c->x + c->w + dgeo.right_width >= c->mon->wx + c->mon->ww)
        c->x = c->mon->wx + c->mon->ww - c->w - dgeo.right_width;
    if (c->y + c->h + dgeo.bottom_height >= c->mon->wy + c->mon->wh)
        c->y = c->mon->wy + c->mon->wh - c->h - dgeo.bottom_height;
}

void
manage_raisefocus(struct Client *c)
{
    size_t i;

    XRaiseWindow(dpy, c->win);
    XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);

    for (i = DecTopLeft; i <= DecBottomRight; i++)
        XRaiseWindow(dpy, c->decwin[i]);
}

void
manage_setsize(struct Client *c)
{
    XMoveResizeWindow(dpy, c->decwin[DecTopLeft],
                      c->x - dgeo.left_width, c->y - dgeo.top_height,
                      dgeo.left_width, dgeo.top_height);
    XMoveResizeWindow(dpy, c->decwin[DecTop],
                      c->x, c->y - dgeo.top_height,
                      c->w, dgeo.top_height);
    XMoveResizeWindow(dpy, c->decwin[DecTopRight],
                      c->x + c->w, c->y - dgeo.top_height,
                      dgeo.right_width, dgeo.top_height);

    XMoveResizeWindow(dpy, c->decwin[DecLeft],
                      c->x - dgeo.left_width, c->y,
                      dgeo.left_width, c->h);
    XMoveResizeWindow(dpy, c->decwin[DecRight],
                      c->x + c->w, c->y,
                      dgeo.right_width, c->h);

    XMoveResizeWindow(dpy, c->decwin[DecBottomLeft],
                      c->x - dgeo.left_width, c->y + c->h,
                      dgeo.left_width, dgeo.bottom_height);
    XMoveResizeWindow(dpy, c->decwin[DecBottom],
                      c->x, c->y + c->h,
                      c->w, dgeo.bottom_height);
    XMoveResizeWindow(dpy, c->decwin[DecBottomRight],
                      c->x + c->w, c->y + c->h,
                      dgeo.right_width, dgeo.bottom_height);

    XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
}

void
run(void)
{
    XEvent ev;

    while (running)
    {
        XNextEvent(dpy, &ev);
        fprintf(stderr, __NAME__": Event %d\n", ev.type);
        if (x11_handler[ev.type])
            x11_handler[ev.type](&ev);
    }
}

void
setup(void)
{
    XRRCrtcInfo *ci;
    XRRScreenResources *sr;
    struct Monitor *m;
    int c;
    XSetWindowAttributes wa = {
        .override_redirect = True,
        .background_pixmap = ParentRelative,
        .event_mask = ExposureMask,
    };

    dpy = XOpenDisplay(NULL);
    root = DefaultRootWindow(dpy);
    screen = DefaultScreen(dpy);
    xerrorxlib = XSetErrorHandler(xerror);

    sr = XRRGetScreenResources(dpy, root);
    for (c = 0; c < sr->ncrtc; c++)
    {
        ci = XRRGetCrtcInfo(dpy, sr, sr->crtcs[c]);
        if (ci == NULL || ci->noutput == 0 || ci->mode == None)
            continue;

        /* TODO Ignore mirrors. */

        m = malloc(sizeof (struct Monitor));
        if (selmon == NULL)
            selmon = m;
        m->wx = m->mx = ci->x;
        m->wy = m->my = ci->y;
        m->ww = m->mw = ci->width;
        m->wh = m->mh = ci->height;
        m->next = monitors;
        monitors = m;
        fprintf(stderr, __NAME__": monitor: %d %d %d %d\n",
                ci->x, ci->y, ci->width, ci->height);
    }

    decorations_load();

    XSelectInput(dpy, root, 0
                 /* Manage creation and destruction of windows */
                 | SubstructureRedirectMask | SubstructureNotifyMask
                 );

    /* Setup invisible window for the client to send messages to */
    command_window = XCreateWindow(
            dpy, root, 0, 0, 10, 10, 0,
            DefaultDepth(dpy, screen),
            CopyFromParent, DefaultVisual(dpy, screen),
            CWOverrideRedirect|CWBackPixmap|CWEventMask,
            &wa
    );
    XChangeProperty(
            dpy, root, XInternAtom(
                    dpy, "_"__NAME_UPPERCASE__"_COMMAND_WINDOW", False
            ),
            XA_WINDOW, 32, PropModeReplace, (unsigned char *)&command_window, 1
    );
}

void
scan(void)
{
    unsigned int i, num;
    Window d1, d2, *wins = NULL;
    XWindowAttributes wa;

    /* TODO grab the server while doing this. */

    /* TODO dwm splits this: First, it looks at non-transient windows
     * and then at transient ones. dwm's manage() always places
     * transient windows on the same monitor/tags as their "parents".
     *
     * We'll be doing the same at some later stage of development. */

    /* TODO We ignore iconified windows for now. */

    if (XQueryTree(dpy, root, &d1, &d2, &wins, &num))
    {
        fprintf(stderr, __NAME__": scan() saw %d windows\n", num);

        for (i = 0; i < num; i++)
        {
            if (!XGetWindowAttributes(dpy, wins[i], &wa) || wa.override_redirect)
                continue;
            if (wa.map_state == IsViewable)
                manage(wins[i], &wa);
        }
        if (wins)
            XFree(wins);
    }
}

void
shutdown(void)
{
    XCloseDisplay(dpy);
}

void
unmanage(struct Client *c)
{
    struct Client **tc;

    for (tc = &clients; *tc && *tc != c; tc = &(*tc)->next);
    *tc = c->next;

    decorations_destroy(c);

    fprintf(stderr, __NAME__": No longer managing window %lu (%p)\n",
            c->win, (void *)c);

    free(c);
}

int
xerror(Display *dpy, XErrorEvent *ee)
{
    /* Taken from dwm */

    /* There's no way to check accesses to destroyed windows, thus those
     * cases are ignored (especially on UnmapNotify's). Other types of
     * errors call Xlibs default error handler, which may call exit. */

    if (ee->error_code == BadWindow
    || (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
    || (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
    || (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
    || (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
    || (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
    || (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
    || (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
    || (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
        return 0;
    fprintf(stderr, __NAME__": Fatal error: request code=%d, error code=%d\n",
            ee->request_code, ee->error_code);
    return xerrorxlib(dpy, ee); /* may call exit */
}

int
main()
{
    setup();
    scan();
    run();
    shutdown();

    exit(EXIT_SUCCESS);
}
