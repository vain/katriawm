/*
 * http://www.x.org/wiki/UserDocumentation/
 * http://www.x.org/releases/X11R7.7/doc/libX11/libX11/libX11.html
 * http://seasonofcode.com/posts/how-x-window-managers-work-and-how-to-write-one-part-i.html
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>

#include "util.h"
#include "ipc.h"

enum DecorationLocation
{
    DecTopLeft = 0,    DecTop = 1,    DecTopRight = 2,
    DecLeft = 3,       DecRight = 4,
    DecBottomLeft = 5, DecBottom = 6, DecBottomRight = 7,

    DecLAST = 8
};

enum DecorationWindowLocation
{
    DecWinTop = 0,
    DecWinLeft = 1,
    DecWinRight = 2,
    DecWinBottom = 3,

    DecWinLAST = 4
};

struct DecorationGeometry
{
    int top_height;
    int left_width, right_width;
    int bottom_height;
};

struct SubImage
{
    int x, y, w, h;
};

enum DecTint
{
    DecTintNormal = 0,
    DecTintSelect = 1,
    DecTintUrgent = 2,

    DecTintLAST = 3
};

#include "pixmaps.h"

#define WORKSPACE_MIN 1
#define WORKSPACE_MAX 127

struct Client
{
    Window win;

    /* Inner size of the actual client */
    int x, y, w, h;

    int visible_x;
    char hidden;

    struct Monitor *mon;
    int workspace;

    Window decwin[DecWinLAST];

    struct Client *next;
};

struct Monitor
{
    int index;
    int active_workspace;

    int layouts[WORKSPACE_MAX + 1];

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
static Cursor cursor_normal;
static Display *dpy;
static XImage *dec_ximg[DecTintLAST];
static Window root, command_window;
static int monitors_num = 0;
static int running = 1;
static int restart = 0;
static int screen;
static int (*xerrorxlib)(Display *, XErrorEvent *);

static struct Client *client_get_for_decoration(
        Window win,
        enum DecorationWindowLocation *which
);
static struct Client *client_get_for_window(Window win);
static void client_save(struct Client *c);
static void decorations_create(struct Client *c);
static void decorations_destroy(struct Client *c);
static void decorations_draw_for_client(struct Client *c, enum DecTint tint,
                                        Pixmap *pm, GC *gc);
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
static void ipc_layout(char arg);
static void ipc_mouse_move(char arg);
static void ipc_mouse_resize(char arg);
static void ipc_nav_monitor(char arg);
static void ipc_nav_workspace_adj(char arg);
static void ipc_nav_workspace(char arg);
static void ipc_quit(char arg);
static void ipc_restart(char arg);
static void layout_float(struct Monitor *m);
static void layout_monocle(struct Monitor *m);
static void layout_tile(struct Monitor *m);
static void manage(Window win, XWindowAttributes *wa);
static void manage_arrange(struct Monitor *m);
static void manage_fit_on_monitor(struct Client *c);
static void manage_goto_workspace(int i);
static void manage_showhide(struct Client *c, char hide);
static void manage_raisefocus(struct Client *c);
static void manage_setsize(struct Client *c);
static void run(void);
static void scan(void);
static void setup(void);
static void shutdown(void);
static void unmanage(struct Client *c);
static int xerror(Display *dpy, XErrorEvent *ee);

static void (*ipc_handler[IPCLast]) (char arg) = {
    [IPCLayout] = ipc_layout,
    [IPCMouseMove] = ipc_mouse_move,
    [IPCMouseResize] = ipc_mouse_resize,
    [IPCNavMonitor] = ipc_nav_monitor,
    [IPCNavWorkspaceAdj] = ipc_nav_workspace_adj,
    [IPCNavWorkspace] = ipc_nav_workspace,
    [IPCQuit] = ipc_quit,
    [IPCRestart] = ipc_restart,
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

static void (*layouts[LALast]) (struct Monitor *m) = {
    /* Index 0 is the default layout, see ipc.h */
    [LAFloat] = layout_float,
    [LAMonocle] = layout_monocle,
    [LATile] = layout_tile,
};

struct Client *
client_get_for_decoration(Window win, enum DecorationWindowLocation *which)
{
    struct Client *c;
    size_t i;

    for (c = clients; c; c = c->next)
    {
        for (i = DecWinTop; i <= DecWinBottom; i++)
        {
            if (c->decwin[i] == win)
            {
                *which = i;
                return c;
            }
        }
    }

    *which = DecWinLAST;
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

    for (i = DecWinTop; i <= DecWinBottom; i++)
    {
        c->decwin[i] = XCreateWindow(
                dpy, root, 0, 0, 10, 10, 0,
                DefaultDepth(dpy, screen),
                CopyFromParent, DefaultVisual(dpy, screen),
                CWOverrideRedirect|CWBackPixmap|CWEventMask,
                &wa
        );
        XMapRaised(dpy, c->decwin[i]);
    }
}

void
decorations_destroy(struct Client *c)
{
    size_t i;

    for (i = DecWinTop; i <= DecWinBottom; i++)
    {
        XUnmapWindow(dpy, c->decwin[i]);
        XDestroyWindow(dpy, c->decwin[i]);
    }
}

void
decorations_draw_for_client(struct Client *c, enum DecTint tint,
                            Pixmap *pm, GC *gc)
{
    int x, y, w, h;

    w = dgeo.left_width + c->w + dgeo.right_width;
    h = dgeo.top_height + c->h + dgeo.bottom_height;

    /* These have to be cleaned up by the caller: */
    *gc = XCreateGC(dpy, root, 0, NULL);
    *pm = XCreatePixmap(dpy, root, w, h, DefaultDepth(dpy, screen));

    /* TODO optimize for speed, use tiling */

    for (x = 0; x < w; x += dec_coords[DecTop].w)
    {
        XPutImage(dpy, *pm, *gc, dec_ximg[tint],
                  dec_coords[DecTop].x, dec_coords[DecTop].y,
                  x, 0,
                  dec_coords[DecTop].w, dec_coords[DecTop].h);
    }

    for (x = 0; x < w; x += dec_coords[DecBottom].w)
    {
        XPutImage(dpy, *pm, *gc, dec_ximg[tint],
                  dec_coords[DecBottom].x, dec_coords[DecBottom].y,
                  x, h - dgeo.bottom_height,
                  dec_coords[DecBottom].w, dec_coords[DecBottom].h);
    }

    for (y = 0; y < c->h; y += dec_coords[DecLeft].h)
    {
        XPutImage(dpy, *pm, *gc, dec_ximg[tint],
                  dec_coords[DecLeft].x, dec_coords[DecLeft].y,
                  0, dgeo.top_height + y,
                  dec_coords[DecLeft].w, dec_coords[DecLeft].h);
    }

    for (y = 0; y < c->h; y += dec_coords[DecRight].h)
    {
        XPutImage(dpy, *pm, *gc, dec_ximg[tint],
                  dec_coords[DecRight].x, dec_coords[DecRight].y,
                  dgeo.left_width + c->w, dgeo.top_height + y,
                  dec_coords[DecRight].w, dec_coords[DecRight].h);
    }

    XPutImage(dpy, *pm, *gc, dec_ximg[tint],
              dec_coords[DecTopLeft].x, dec_coords[DecTopLeft].y,
              0, 0,
              dec_coords[DecTopLeft].w, dec_coords[DecTopLeft].h);

    XPutImage(dpy, *pm, *gc, dec_ximg[tint],
              dec_coords[DecTopRight].x, dec_coords[DecTopRight].y,
              w - dec_coords[DecTopRight].w, 0,
              dec_coords[DecTopRight].w, dec_coords[DecTopRight].h);

    XPutImage(dpy, *pm, *gc, dec_ximg[tint],
              dec_coords[DecBottomLeft].x, dec_coords[DecBottomLeft].y,
              0, h - dec_coords[DecBottomLeft].h,
              dec_coords[DecBottomLeft].w, dec_coords[DecBottomLeft].h);

    XPutImage(dpy, *pm, *gc, dec_ximg[tint],
              dec_coords[DecBottomRight].x, dec_coords[DecBottomRight].y,
              w - dec_coords[DecTopRight].w, h - dec_coords[DecBottomLeft].h,
              dec_coords[DecBottomRight].w, dec_coords[DecBottomRight].h);
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
                        data, dec_img_w, dec_img_h, 32, 0);
}

void
handle_clientmessage(XEvent *e)
{
    XClientMessageEvent *cme = &e->xclient;
    static Atom t = None;
    enum IPCCommand cmd;
    char arg;

    if (t == None)
        t = XInternAtom(dpy, IPC_ATOM_COMMAND, False);

    if (cme->message_type != t)
    {
        fprintf(stderr,
                __NAME_WM__": Received client message with unknown type");
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
    struct Monitor *m;

    if ((c = client_get_for_window(ev->window)))
    {
        m = c->mon;
        unmanage(c);
        manage_arrange(m);
    }
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
    enum DecorationWindowLocation which = DecWinLAST;
    enum DecTint tint = DecTintSelect;
    GC gc;
    Pixmap dec_pm;

    if ((c = client_get_for_decoration(ev->window, &which)) == NULL)
        return;

    decorations_draw_for_client(c, tint, &dec_pm, &gc);

    switch (which)
    {
        case DecWinTop:
            XCopyArea(dpy, dec_pm, c->decwin[which], gc,
                      0, 0,
                      dgeo.left_width + c->w + dgeo.right_width, dgeo.top_height,
                      0, 0);
            break;
        case DecWinLeft:
            XCopyArea(dpy, dec_pm, c->decwin[which], gc,
                      0, dgeo.top_height,
                      dgeo.left_width, c->h,
                      0, 0);
            break;
        case DecWinRight:
            XCopyArea(dpy, dec_pm, c->decwin[which], gc,
                      dgeo.left_width + c->w, dgeo.top_height,
                      dgeo.right_width, c->h,
                      0, 0);
            break;
        case DecWinBottom:
            XCopyArea(dpy, dec_pm, c->decwin[which], gc,
                      0, dgeo.top_height + c->h,
                      dgeo.left_width + c->w + dgeo.right_width, dgeo.bottom_height,
                      0, 0);
            break;
        case DecWinLAST:
            /* ignore */
            break;
    }

    XFreePixmap(dpy, dec_pm);
    XFreeGC(dpy, gc);
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
    struct Monitor *m;

    if ((c = client_get_for_window(ev->window)))
    {
        m = c->mon;
        unmanage(c);
        manage_arrange(m);
    }
}

void
ipc_layout(char arg)
{
    int i = arg;

    if (layouts[i] == NULL)
    {
        fprintf(stderr, __NAME_WM__": Invalid layout requested: %d\n", i);
        return;
    }

    selmon->layouts[selmon->active_workspace] = i;
    manage_arrange(selmon);
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
        DPRINTF(__NAME_WM__": Mouse move: down at %d, %d over %lu\n",
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
        DPRINTF(__NAME_WM__": Mouse move: motion to %d, %d\n", x, y);

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
        DPRINTF( __NAME_WM__": Mouse resize: down at %d, %d over %lu\n",
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
        DPRINTF(__NAME_WM__": Mouse resize: motion to %d, %d\n", x, y);

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
ipc_nav_monitor(char arg)
{
    int i;
    struct Monitor *m;

    i = selmon->index;
    i += arg;
    i = i < 0 ? 0 : i;
    i = i >= monitors_num ? monitors_num - 1 : i;

    for (m = monitors; m; m = m->next)
    {
        if (m->index == i)
        {
            selmon = m;
            return;
        }
    }

    /* TODO focus client */
}

void
ipc_nav_workspace(char arg)
{
    int i;

    i = arg;
    manage_goto_workspace(i);
}

void
ipc_nav_workspace_adj(char arg)
{
    int i;

    i = selmon->active_workspace;
    i += arg;
    manage_goto_workspace(i);
}

void
ipc_quit(char arg)
{
    (void)arg;

    running = 0;

    DPRINTF(__NAME_WM__": Quitting\n");
}

void
ipc_restart(char arg)
{
    (void)arg;

    restart = 1;
    running = 0;

    DPRINTF(__NAME_WM__": Quitting for restart\n");
}

void
layout_float(struct Monitor *m)
{
    (void)m;
}

void
layout_monocle(struct Monitor *m)
{
    /* XXX untested */

    struct Client *c;

    for (c = clients; c; c = c->next)
    {
        if (c->mon == m && c->workspace == m->active_workspace)
        {
            c->x = c->mon->wx + dgeo.left_width;
            c->y = c->mon->wy + dgeo.top_height;
            c->w = c->mon->ww - dgeo.left_width - dgeo.right_width;
            c->h = c->mon->wh - dgeo.top_height - dgeo.bottom_height;
            manage_setsize(c);
        }
    }
}

void
layout_tile(struct Monitor *m)
{
    struct Client *c;
    int i = 0;
    int num_clients = 0, at_y = 0, slave_h, master_w;

    /* Note: at_y, slave_h and master_w all the *visible* sizes
     * including decorations */

    for (c = clients; c; c = c->next)
        if (c->mon == m && c->workspace == m->active_workspace)
            num_clients++;

    for (c = clients; c; c = c->next)
    {
        if (c->mon == m && c->workspace == m->active_workspace)
        {
            if (i == 0)
            {
                if (num_clients == 1)
                {
                    /* Only one client total, just maximize it */
                    c->x = c->mon->wx + dgeo.left_width;
                    c->y = c->mon->wy + dgeo.top_height;
                    c->w = c->mon->ww - dgeo.left_width - dgeo.right_width;
                    c->h = c->mon->wh - dgeo.top_height - dgeo.bottom_height;
                }
                else
                {
                    /* More than one client, place first client in
                     * master column */
                    master_w = c->mon->ww / 2;

                    c->x = c->mon->wx + dgeo.left_width;
                    c->y = c->mon->wy + dgeo.top_height;
                    c->w = master_w - dgeo.left_width - dgeo.right_width;
                    c->h = c->mon->wh - dgeo.top_height - dgeo.bottom_height;
                }
            }
            else
            {
                /* Slave column, use remaining width and accumulate y
                 * offset */
                c->x = c->mon->wx + master_w + dgeo.left_width;
                c->y = at_y + dgeo.top_height;
                c->w = (c->mon->ww - master_w) - dgeo.left_width
                       - dgeo.right_width;

                if (i == num_clients - 1)
                    slave_h = c->mon->wh - at_y;
                else
                    slave_h = c->mon->wh / (num_clients - 1);

                c->h = slave_h - dgeo.top_height - dgeo.bottom_height;

                at_y += slave_h;
            }

            manage_setsize(c);
            i++;
        }
    }
}

void
manage(Window win, XWindowAttributes *wa)
{
    struct Client *c;

    if (client_get_for_window(win))
    {
        fprintf(stderr, __NAME_WM__": Window %lu is already known, won't map\n",
                win);
        return;
    }

    c = calloc(1, sizeof (struct Client));

    c->win = win;
    c->mon = selmon;
    c->workspace = selmon->active_workspace;

    c->x = wa->x;
    c->y = wa->y;
    c->w = wa->width;
    c->h = wa->height;

    XSetWindowBorderWidth(dpy, c->win, 0);

    XSelectInput(dpy, c->win, 0
                              /* Focus */
                              | EnterWindowMask
                              );

    decorations_create(c);
    manage_fit_on_monitor(c);
    manage_setsize(c);

    client_save(c);

    DPRINTF(__NAME_WM__": Managing window %lu (%p) at %dx%d+%d+%d\n",
            c->win, (void *)c, c->w, c->h, c->x, c->y);

    XMapWindow(dpy, c->win);
    manage_arrange(c->mon);
    manage_raisefocus(c);
}

void
manage_arrange(struct Monitor *m)
{
    DPRINTF(__NAME_WM__": Arranging monitor %p\n", (void *)m);
    layouts[m->layouts[m->active_workspace]](m);
}

void
manage_fit_on_monitor(struct Client *c)
{
    if (c->mon == NULL)
    {
        fprintf(stderr, __NAME_WM__": No monitor assigned to %lu (%p)\n",
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
manage_goto_workspace(int i)
{
    struct Client *c;

    i = i < WORKSPACE_MIN ? WORKSPACE_MIN : i;
    i = i > WORKSPACE_MAX ? WORKSPACE_MAX : i;

    DPRINTF(__NAME_WM__": Changing to workspace %d\n", i);

    for (c = clients; c; c = c->next)
        if (c->mon == selmon)
            manage_showhide(c, 1);

    for (c = clients; c; c = c->next)
        if (c->mon == selmon && c->workspace == i)
            manage_showhide(c, 0);

    selmon->active_workspace = i;
}

void
manage_raisefocus(struct Client *c)
{
    size_t i;

    XRaiseWindow(dpy, c->win);
    XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);

    for (i = DecWinTop; i <= DecWinBottom; i++)
        XRaiseWindow(dpy, c->decwin[i]);
}

void
manage_showhide(struct Client *c, char hide)
{
    if (hide && !c->hidden)
    {
        c->visible_x = c->x;
        c->x = -2 * c->w;
        c->hidden = 1;

        manage_setsize(c);
    }

    if (!hide)
    {
        c->x = c->visible_x;
        c->hidden = 0;

        manage_setsize(c);
    }
}

void
manage_setsize(struct Client *c)
{
    if (c->w <= 0)
        c->w = 1;
    if (c->h <= 0)
        c->h = 1;

    DPRINTF(__NAME_WM__": Moving client %p to %d, %d with size %d, %d\n",
            (void *)c, c->x, c->y, c->w, c->h);

    XMoveResizeWindow(dpy, c->decwin[DecWinTop],
                      c->x - dgeo.left_width, c->y - dgeo.top_height,
                      dgeo.left_width + c->w + dgeo.right_width,
                      dgeo.top_height);
    XMoveResizeWindow(dpy, c->decwin[DecWinLeft],
                      c->x - dgeo.left_width, c->y,
                      dgeo.left_width, c->h);
    XMoveResizeWindow(dpy, c->decwin[DecWinRight],
                      c->x + c->w, c->y,
                      dgeo.right_width, c->h);
    XMoveResizeWindow(dpy, c->decwin[DecWinBottom],
                      c->x - dgeo.left_width, c->y + c->h,
                      dgeo.left_width + c->w + dgeo.right_width,
                      dgeo.bottom_height);

    XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
}

void
run(void)
{
    XEvent ev;

    while (running)
    {
        XNextEvent(dpy, &ev);
        DPRINTF(__NAME_WM__": Event %d\n", ev.type);
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
    int c, cinner;
    int minx, minindex;
    char *chosen = NULL;
    XSetWindowAttributes wa = {
        .override_redirect = True,
        .background_pixmap = ParentRelative,
        .event_mask = ExposureMask,
    };

    dpy = XOpenDisplay(NULL);
    root = DefaultRootWindow(dpy);
    screen = DefaultScreen(dpy);
    xerrorxlib = XSetErrorHandler(xerror);

    /* TODO handle monitor setup changes during runtime */

    sr = XRRGetScreenResources(dpy, root);
    assert(sr->ncrtc > 0);
    chosen = calloc(sr->ncrtc, sizeof (char));
    for (c = 0; c < sr->ncrtc; c++)
    {
        /* Always sort monitors by their X offset. */
        minx = -1;
        minindex = -1;
        for (cinner = 0; cinner < sr->ncrtc; cinner++)
        {
            ci = XRRGetCrtcInfo(dpy, sr, sr->crtcs[cinner]);
            if (ci == NULL || ci->noutput == 0 || ci->mode == None)
                continue;

            if (chosen[cinner] == 0 && (minx == -1 || ci->x < minx))
            {
                minx = ci->x;
                minindex = cinner;
            }
        }
        if (minindex == -1)
            continue;

        ci = XRRGetCrtcInfo(dpy, sr, sr->crtcs[minindex]);
        chosen[minindex] = 1;

        /* TODO Ignore mirrors. */

        m = malloc(sizeof (struct Monitor));
        if (selmon == NULL)
            selmon = m;
        m->wx = m->mx = ci->x;
        m->wy = m->my = ci->y;
        m->ww = m->mw = ci->width;
        m->wh = m->mh = ci->height;
        m->index = monitors_num++;
        m->active_workspace = 1;
        m->next = monitors;
        monitors = m;
        DPRINTF(__NAME_WM__": monitor: %d %d %d %d\n",
                ci->x, ci->y, ci->width, ci->height);
    }
    free(chosen);

    decorations_load();

    XSelectInput(dpy, root, 0
                 /* Manage creation and destruction of windows */
                 | SubstructureRedirectMask | SubstructureNotifyMask
                 );

    /* Set default cursor on root window */
    cursor_normal = XCreateFontCursor(dpy, XC_left_ptr);
    XDefineCursor(dpy, root, cursor_normal);

    /* Setup invisible window for the client to send messages to */
    command_window = XCreateWindow(
            dpy, root, 0, 0, 10, 10, 0,
            DefaultDepth(dpy, screen),
            CopyFromParent, DefaultVisual(dpy, screen),
            CWOverrideRedirect|CWBackPixmap|CWEventMask,
            &wa
    );
    XChangeProperty(dpy, root, XInternAtom(dpy, IPC_ATOM_WINDOW, False),
                    XA_WINDOW, 32, PropModeReplace,
                    (unsigned char *)&command_window, 1);
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
        DPRINTF(__NAME_WM__": scan() saw %d windows\n", num);

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
    size_t i;

    while (clients != NULL)
        unmanage(clients);

    for (i = DecTintNormal; i <= DecTintUrgent; i++)
        XDestroyImage(dec_ximg[i]);

    XUnmapWindow(dpy, command_window);
    XDestroyWindow(dpy, command_window);

    XFreeCursor(dpy, cursor_normal);

    XDeleteProperty(dpy, root, XInternAtom(dpy, IPC_ATOM_WINDOW, False));

    XCloseDisplay(dpy);
}

void
unmanage(struct Client *c)
{
    struct Client **tc;

    for (tc = &clients; *tc && *tc != c; tc = &(*tc)->next);
    *tc = c->next;

    decorations_destroy(c);

    DPRINTF(__NAME_WM__": No longer managing window %lu (%p)\n",
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
    fprintf(stderr,
            __NAME_WM__": Fatal error: request code=%d, error code=%d\n",
            ee->request_code, ee->error_code);
    return xerrorxlib(dpy, ee); /* may call exit */
}

int
main(int argc, char **argv)
{
    (void)argc;

    setup();
    scan();
    run();
    shutdown();

    if (restart)
        execvp(argv[0], argv);

    exit(EXIT_SUCCESS);
}
