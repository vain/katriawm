/*
 * http://www.x.org/wiki/UserDocumentation/
 * http://www.x.org/releases/X11R7.7/doc/libX11/libX11/libX11.html
 * http://seasonofcode.com/posts/how-x-window-managers-work-and-how-to-write-one-part-i.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/extensions/Xrandr.h>

enum DecorationLocation
{
    DecTopLeft = 0,    DecTop = 1,    DecTopRight = 2,
    DecLeft = 3,       DecRight = 4,
    DecBottomLeft = 5, DecBottom = 6, DecBottomRight = 7,

    DecLAST = 8
};

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

struct DecorationGeometry
{
    int top_height;
    int left_width, right_width;
    int bottom_height;
} dgeo;

struct Monitor
{
    /* Actual monitor size */
    int mx, my, mw, mh;

    /* Logical size, i.e. where we can place windows */
    int wx, wy, ww, wh;

    struct Monitor *next;
};

static struct Client *clients = NULL;
static struct Client *mdc = NULL;
static struct Monitor *monitors = NULL, *selmon = NULL;
static int mdx, mdy, ocx, ocy, ocw, och;
static Display *dpy;
static Window root;
static int running = 1;
static int screen;
static int (*xerrorxlib)(Display *, XErrorEvent *);

static struct Client *client_get_for_decoration(Window win,
                                                enum DecorationLocation *which);
static struct Client *client_get_for_window(Window win);
static void client_save(struct Client *c);
static void decorations_create(struct Client *c);
static void decorations_destroy(struct Client *c);
static void handle_button(XEvent *e);
static void handle_configurerequest(XEvent *e);
static void handle_destroynotify(XEvent *e);
static void handle_enternotify(XEvent *e);
static void handle_expose(XEvent *e);
static void handle_maprequest(XEvent *e);
static void handle_motionnotify(XEvent *e);
static void handle_unmapnotify(XEvent *e);
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

static void (*handler[LASTEvent]) (XEvent *) = {
    [ButtonPress] = handle_button,
    [ButtonRelease] = handle_button,
    [ConfigureRequest] = handle_configurerequest,
    [DestroyNotify] = handle_destroynotify,
    [EnterNotify] = handle_enternotify,
    [Expose] = handle_expose,
    [MapRequest] = handle_maprequest,
    [MotionNotify] = handle_motionnotify,
    [UnmapNotify] = handle_unmapnotify,
};

#include "config.h"

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
handle_button(XEvent *e)
{
    XButtonEvent *ev = &e->xbutton;
    struct Client *c;

    /* Reset to NULL on ButtonRelease and on clicks that don't belong to
     * a known client. */
    mdc = NULL;

    if (e->type == ButtonPress && ev->state & MODMASK)
    {
        if ((c = client_get_for_window(ev->window)))
        {
            mdc = c;
            mdx = ev->x_root;
            mdy = ev->y_root;
            ocx = c->x;
            ocy = c->y;
            ocw = c->w;
            och = c->h;

            manage_raisefocus(c);
        }
    }
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
    long unsigned int democolor;

    if ((c = client_get_for_decoration(ev->window, &which)) == NULL)
        return;

    democolor = 0xFF000000 |
                ((which & 1) * 0xFF) |
                (((which & 2) >> 1) * 0xFF00) |
                (((which & 4) >> 2) * 0xFF0000);

    switch (which)
    {
        case DecTopLeft:
            XSetForeground(dpy, c->decwin_gc[which], democolor);
            XFillRectangle(dpy, c->decwin[which], c->decwin_gc[which],
                           0, 0,
                           dgeo.left_width, dgeo.top_height);
            break;
        case DecTop:
            XSetForeground(dpy, c->decwin_gc[which], democolor);
            XFillRectangle(dpy, c->decwin[which], c->decwin_gc[which],
                           0, 0,
                           c->w, dgeo.top_height);
            break;
        case DecTopRight:
            XSetForeground(dpy, c->decwin_gc[which], democolor);
            XFillRectangle(dpy, c->decwin[which], c->decwin_gc[which],
                           0, 0,
                           dgeo.right_width, dgeo.top_height);
            break;

        case DecLeft:
            XSetForeground(dpy, c->decwin_gc[which], democolor);
            XFillRectangle(dpy, c->decwin[which], c->decwin_gc[which],
                           0, 0,
                           dgeo.left_width, c->h);
            break;
        case DecRight:
            XSetForeground(dpy, c->decwin_gc[which], democolor);
            XFillRectangle(dpy, c->decwin[which], c->decwin_gc[which],
                           0, 0,
                           dgeo.right_width, c->h);
            break;

        case DecBottomLeft:
            XSetForeground(dpy, c->decwin_gc[which], democolor);
            XFillRectangle(dpy, c->decwin[which], c->decwin_gc[which],
                           0, 0,
                           dgeo.left_width, dgeo.bottom_height);
            break;
        case DecBottom:
            XSetForeground(dpy, c->decwin_gc[which], democolor);
            XFillRectangle(dpy, c->decwin[which], c->decwin_gc[which],
                           0, 0,
                           c->w, dgeo.bottom_height);
            break;
        case DecBottomRight:
            XSetForeground(dpy, c->decwin_gc[which], democolor);
            XFillRectangle(dpy, c->decwin[which], c->decwin_gc[which],
                           0, 0,
                           dgeo.right_width, dgeo.bottom_height);
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
handle_motionnotify(XEvent *e)
{
    XMotionEvent *ev = &e->xmotion;
    XEvent dummy;
    int dx, dy;

    while (XCheckTypedWindowEvent(dpy, ev->window, MotionNotify, &dummy));

    if (mdc == NULL)
        return;

    dx = ev->x_root - mdx;
    dy = ev->y_root - mdy;

    if (ev->state & Button1Mask)
    {
        mdc->x = ocx + dx;
        mdc->y = ocy + dy;
    }
    else if (ev->state & Button3Mask)
    {
        mdc->w = ocw + dx;
        mdc->h = och + dy;
    }

    manage_setsize(mdc);
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
#if 0
                              /* Moving, resizing, raising */
                              /* Only works on the window border ...? */
                              | ButtonPressMask | ButtonReleaseMask
                              | ButtonMotionMask
#endif
                              /* Focus */
                              | EnterWindowMask
                              );

    /* TODO Mod2Mask is a static hackaround numlockmask (probably) */
    XGrabButton(dpy, Button1, MODMASK | Mod2Mask, c->win, False,
                ButtonPressMask | ButtonReleaseMask | ButtonMotionMask,
                GrabModeAsync, GrabModeAsync, None, None);
    XGrabButton(dpy, Button3, MODMASK | Mod2Mask, c->win, False,
                ButtonPressMask | ButtonReleaseMask | ButtonMotionMask,
                GrabModeAsync, GrabModeAsync, None, None);

    XMapWindow(dpy, c->win);

    decorations_create(c);
    manage_fit_on_monitor(c);
    manage_setsize(c);

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
        if (handler[ev.type])
            handler[ev.type](&ev);
    }
}

void
setup(void)
{
    XRRCrtcInfo *ci;
    XRRScreenResources *sr;
    struct Monitor *m;
    int c;

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

    /* TODO derive from font size and/or pixmap size */
    dgeo.top_height = 16; dgeo.left_width = 2; dgeo.right_width = 2;
    dgeo.bottom_height = 2;

    XSelectInput(dpy, root, 0
                 /* Manage creation and destruction of windows */
                 | SubstructureRedirectMask | SubstructureNotifyMask
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
