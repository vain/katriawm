/*
 * http://www.x.org/wiki/UserDocumentation/
 * http://www.x.org/releases/X11R7.7/doc/libX11/libX11/libX11.html
 * http://seasonofcode.com/posts/how-x-window-managers-work-and-how-to-write-one-part-i.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>

struct Client
{
    int x, y, w, h;      /* Visible, "outer" size */
    Window win;

    struct Client *next;
};

static struct Client *clients = NULL;
static struct Client *mdc = NULL;
static int mdx, mdy, ocx, ocy, ocw, och;
static Display *dpy;
static Window root;
static int running = 1;
static int screen;
static int (*xerrorxlib)(Display *, XErrorEvent *);

static struct Client *client_get_for_window(Window win);
static void client_save(struct Client *c);
static void handle_button(XEvent *e);
static void handle_configurerequest(XEvent *e);
static void handle_destroynotify(XEvent *e);
static void handle_maprequest(XEvent *e);
static void handle_motionnotify(XEvent *e);
static void handle_unmapnotify(XEvent *e);
static void manage(Window win, XWindowAttributes *wa);
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
    [MapRequest] = handle_maprequest,
    [MotionNotify] = handle_motionnotify,
    [UnmapNotify] = handle_unmapnotify,
#if 0
    [EnterNotify] = handle_enternotify,
    [Expose] = handle_expose,
#endif
};

#include "config.h"

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

            XRaiseWindow(dpy, c->win);
            XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
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

    c = calloc(1, sizeof(struct Client));

    c->x = wa->x;
    c->y = wa->y;
    c->w = wa->width;
    c->h = wa->height;
    c->win = win;

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

    client_save(c);

    fprintf(stderr, __NAME__": Managing window %lu (%p) at %dx%d+%d+%d\n",
            c->win, (void *)c, c->w, c->h, c->x, c->y);
}

void
manage_setsize(struct Client *c)
{
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
    dpy = XOpenDisplay(NULL);
    root = DefaultRootWindow(dpy);
    screen = DefaultScreen(dpy);
    xerrorxlib = XSetErrorHandler(xerror);

    /* TODO scan monitors */

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
