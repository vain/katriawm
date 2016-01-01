/*
 * http://www.x.org/wiki/UserDocumentation/
 * http://www.x.org/releases/X11R7.7/doc/libX11/libX11/libX11.html
 * http://seasonofcode.com/posts/how-x-window-managers-work-and-how-to-write-one-part-i.html
 */

#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>

#include "util.h"
#include "ipc.h"

#include "theme_types.h"
#include "theme.h"

#define WORKSPACE_MIN 1
#define WORKSPACE_MAX 127
#define WM_NAME_UNKNOWN "<name unknown>"
#define VIS_ON_SELMON(c) ((c)->mon == selmon && \
                          (c)->workspace == selmon->active_workspace)
#define VIS_ON_M(c, m) ((c)->mon == (m) && \
                        (c)->workspace == (m)->active_workspace)
#define SOMETHING_FOCUSED (selc && VIS_ON_SELMON(selc))

struct Client
{
    Window win;

    /* Inner size of the actual client, excluding decorations */
    int x, y, w, h;

    int normal_x, normal_y, normal_w, normal_h;
    int nonhidden_x;
    char floating;
    char fullscreen;
    char hidden;
    char urgent;

    char title[512];

    struct Monitor *mon;
    int workspace;

    Window decwin[DecWinLAST];

    struct Client *next;
    struct Client *focus_next;
};

struct Monitor
{
    int index;
    int active_workspace, recent_workspace;

    int layouts[WORKSPACE_MAX + 1];

    /* Actual monitor size */
    int mx, my, mw, mh;

    /* Logical size, i.e. where we can place windows */
    int wx, wy, ww, wh;

    struct Monitor *next;
};

struct WorkareaInsets
{
    int top, left, right, bottom;
};

enum AtomsNet
{
    AtomNetSupported,
    AtomNetWMName,
    AtomNetWMState,
    AtomNetWMStateFullscreen,
    AtomNetWMWindowType,
    AtomNetWMWindowTypeDialog,
    AtomNetWMWindowTypeMenu,
    AtomNetWMWindowTypeSplash,
    AtomNetWMWindowTypeToolbar,
    AtomNetWMWindowTypeUtility,

    AtomNetLAST,
};

enum AtomsWM
{
    AtomWMProtocols,
    AtomWMDeleteWindow,

    AtomWMLAST,
};

#include "config.h"

static struct Client *clients = NULL, *selc = NULL;
static struct Client *mouse_dc = NULL;
static struct Monitor *monitors = NULL, *selmon = NULL;
static int screen_w = -1, screen_h = -1;
static int mouse_dx, mouse_dy, mouse_ocx, mouse_ocy, mouse_ocw, mouse_och;
static Atom atom_net[AtomNetLAST], atom_wm[AtomWMLAST], atom_state, atom_ipc;
static Cursor cursor_normal;
static Display *dpy;
static XftColor font_color[DecTintLAST];
static XftFont *font[FontLAST];
static Pixmap dec_tiles[DecTintLAST][DecLAST];
static Window root;
static int monitors_num = 0, prevmon_i = 0;
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
static void client_update_title(struct Client *c);
static void decorations_create(struct Client *c);
static void decorations_destroy(struct Client *c);
static void decorations_draw_for_client(struct Client *c,
                                        enum DecorationWindowLocation which);
static Pixmap decorations_get_pm(GC gc, XImage **ximg, enum DecorationLocation l,
                                 enum DecTint t);
static void decorations_load(void);
static char *decorations_tint(unsigned long color);
static XImage *decorations_to_ximg(char *data);
static void draw_text(Drawable d, XftFont *xfont, XftColor *col, int x, int y,
                      int w, char *s);
static void handle_clientmessage(XEvent *e);
static void handle_configurenotify(XEvent *e);
static void handle_configurerequest(XEvent *e);
static void handle_destroynotify(XEvent *e);
static void handle_expose(XEvent *e);
static void handle_focusin(XEvent *e);
static void handle_maprequest(XEvent *e);
static void handle_propertynotify(XEvent *e);
static void handle_unmapnotify(XEvent *e);
static void ipc_client_close(char arg);
static void ipc_client_kill(char arg);
static void ipc_client_fullscreen_toggle(char arg);
static void ipc_client_move_list(char arg);
static void ipc_client_move_mouse(char arg);
static void ipc_client_resize_mouse(char arg);
static void ipc_client_select_adjacent(char arg);
static void ipc_client_select_recent(char arg);
static void ipc_client_switch_monitor_adjacent(char arg);
static void ipc_client_switch_workspace(char arg);
static void ipc_client_switch_workspace_adjacent(char arg);
static void ipc_layout_set(char arg);
static void ipc_monitor_select_adjacent(char arg);
static void ipc_monitor_select_recent(char arg);
static void ipc_wm_quit(char arg);
static void ipc_wm_restart(char arg);
static void ipc_workspace_select_adjacent(char arg);
static void ipc_workspace_select_recent(char arg);
static void ipc_workspace_select(char arg);
static void layout_float(struct Monitor *m);
static void layout_monocle(struct Monitor *m);
static void layout_tile(struct Monitor *m);
static void manage(Window win, XWindowAttributes *wa);
static void manage_arrange(struct Monitor *m);
static void manage_client_gone(struct Client *c);
static void manage_fit_on_monitor(struct Client *c);
static void manage_focus_add(struct Client *c);
static void manage_focus_remove(struct Client *c);
static void manage_focus_set(struct Client *c);
static void manage_fullscreen(struct Client *c, char fs);
static void manage_goto_monitor(int i);
static void manage_goto_workspace(int i);
static void manage_showhide(struct Client *c, char hide);
static void manage_raisefocus(struct Client *c);
static void manage_raisefocus_first_matching(void);
static void manage_setsize(struct Client *c);
static void manage_xfocus(struct Client *c);
static void manage_xraise(struct Client *c);
static void publish_state(void);
static void run(void);
static void scan(void);
static void setup(void);
static void setup_hints(void);
static void setup_monitors_read(void);
static void shutdown(void);
static void shutdown_monitors_free(void);
static void unmanage(struct Client *c);
static int xerror(Display *dpy, XErrorEvent *ee);

static void (*ipc_handler[IPCLast]) (char arg) = {
    [IPCClientClose] = ipc_client_close,
    [IPCClientFullscreenToggle] = ipc_client_fullscreen_toggle,
    [IPCClientKill] = ipc_client_kill,
    [IPCClientMoveList] = ipc_client_move_list,
    [IPCClientMoveMouse] = ipc_client_move_mouse,
    [IPCClientResizeMouse] = ipc_client_resize_mouse,
    [IPCClientSelectAdjacent] = ipc_client_select_adjacent,
    [IPCClientSelectRecent] = ipc_client_select_recent,
    [IPCClientSwitchMonitorAdjacent] = ipc_client_switch_monitor_adjacent,
    [IPCClientSwitchWorkspace] = ipc_client_switch_workspace,
    [IPCClientSwitchWorkspaceAdjacent] = ipc_client_switch_workspace_adjacent,
    [IPCLayoutSet] = ipc_layout_set,
    [IPCMonitorSelectAdjacent] = ipc_monitor_select_adjacent,
    [IPCMonitorSelectRecent] = ipc_monitor_select_recent,
    [IPCWMQuit] = ipc_wm_quit,
    [IPCWMRestart] = ipc_wm_restart,
    [IPCWorkspaceSelectAdjacent] = ipc_workspace_select_adjacent,
    [IPCWorkspaceSelectRecent] = ipc_workspace_select_recent,
    [IPCWorkspaceSelect] = ipc_workspace_select,
};

static void (*x11_handler[LASTEvent]) (XEvent *) = {
    [ClientMessage] = handle_clientmessage,
    [ConfigureNotify] = handle_configurenotify,
    [ConfigureRequest] = handle_configurerequest,
    [DestroyNotify] = handle_destroynotify,
    [Expose] = handle_expose,
    [FocusIn] = handle_focusin,
    [MapRequest] = handle_maprequest,
    [PropertyNotify] = handle_propertynotify,
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
                if (which)
                    *which = i;
                return c;
            }
        }
    }

    if (which)
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
client_update_title(struct Client *c)
{
    XTextProperty tp;
    char **slist = NULL;
    int count;

    c->title[0] = 0;

    if (!XGetTextProperty(dpy, c->win, &tp, atom_net[AtomNetWMName]))
    {
        DPRINTF(__NAME_WM__": Title of client %p could not be read from EWMH\n",
                (void *)c);
        if (!XGetTextProperty(dpy, c->win, &tp, XA_WM_NAME))
        {
            DPRINTF(__NAME_WM__": Title of client %p could not be read from X\n",
                    (void *)c);
            strncpy(c->title, WM_NAME_UNKNOWN, sizeof c->title);
            return;
        }
    }

    if (tp.nitems == 0)
    {
        strncpy(c->title, WM_NAME_UNKNOWN, sizeof c->title);
        return;
    }

    if (tp.encoding == XA_STRING)
    {
        strncpy(c->title, (char *)tp.value, sizeof c->title);
        DPRINTF(__NAME_WM__": Title of client %p read as verbatim string\n",
                (void *)c);
    }
    else
    {
        /* This is a particularly gnarly function. Props to dwm which has
         * figured out how to use it. */
        if (XmbTextPropertyToTextList(dpy, &tp, &slist, &count) >= Success
            && count > 0 && *slist)
        {
            strncpy(c->title, slist[0], sizeof c->title - 1);
            XFreeStringList(slist);
            DPRINTF(__NAME_WM__": Title of client %p read as XmbText\n",
                    (void *)c);
        }
    }

    c->title[sizeof c->title - 1] = 0;

    XFree(tp.value);

    DPRINTF(__NAME_WM__": Title of client %p is now '%s'\n", (void *)c,
            c->title);
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
                CWOverrideRedirect | CWBackPixmap | CWEventMask,
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
decorations_draw_for_client(struct Client *c,
                            enum DecorationWindowLocation which)
{
    int x, y, w, h;
    enum DecTint tint = DecTintNormal;
    GC gc, gc_tiled;
    Pixmap dec_pm;

    /* We first create a pixmap with the size of the "visible" client,
     * i.e. the real client window + size of the window decorations.
     * This allows for easier drawing algorithms. After that, the
     * relevant portions will be copied to the real decoration windows.
     *
     * The decorations are drawn like this: First, all four sides will
     * be drawn. This involves patterning/tiling the corresponding parts
     * of the source image onto our pixmap. We will cover every pixel
     * from left to right (or top to bottom). After that, the four
     * corner images will be rendered on top.
     *
     * The title string will be rendered on top of this intermediate
     * result.
     *
     * We always draw the complete decoration, even though only parts of
     * it might actually be copied onto a real window (see parameter
     * "which"). */

    if (c->urgent)
        tint = DecTintUrgent;
    else if (c == selc && c->mon == selmon && VIS_ON_SELMON(c))
        tint = DecTintSelect;

    w = dgeo.left_width + c->w + dgeo.right_width;
    h = dgeo.top_height + c->h + dgeo.bottom_height;

    gc = XCreateGC(dpy, root, 0, NULL);
    gc_tiled = XCreateGC(dpy, root, 0, NULL);
    dec_pm = XCreatePixmap(dpy, root, w, h, DefaultDepth(dpy, screen));

    XSetFillStyle(dpy, gc_tiled, FillTiled);

    XSetTile(dpy, gc_tiled, dec_tiles[tint][DecTop]);
    XSetTSOrigin(dpy, gc_tiled, 0, 0);
    XFillRectangle(dpy, dec_pm, gc_tiled, 0, 0, w, dec_coords[DecTop].h);

    XSetTile(dpy, gc_tiled, dec_tiles[tint][DecBottom]);
    XSetTSOrigin(dpy, gc_tiled, 0, h - dgeo.bottom_height);
    XFillRectangle(dpy, dec_pm, gc_tiled, 0, h - dgeo.bottom_height,
                   w, dec_coords[DecBottom].h);

    XSetTile(dpy, gc_tiled, dec_tiles[tint][DecLeft]);
    XSetTSOrigin(dpy, gc_tiled, 0, dgeo.top_height);
    XFillRectangle(dpy, dec_pm, gc_tiled, 0, dgeo.top_height,
                   dec_coords[DecLeft].w, c->h);

    XSetTile(dpy, gc_tiled, dec_tiles[tint][DecRight]);
    XSetTSOrigin(dpy, gc_tiled, w - dgeo.right_width, dgeo.top_height);
    XFillRectangle(dpy, dec_pm, gc_tiled, w - dgeo.right_width, dgeo.top_height,
                   dec_coords[DecRight].w, c->h);

    XCopyArea(dpy, dec_tiles[tint][DecTopLeft], dec_pm, gc,
              0, 0,
              dec_coords[DecTopLeft].w, dec_coords[DecTopLeft].h,
              0, 0);

    XCopyArea(dpy, dec_tiles[tint][DecTopRight], dec_pm, gc,
              0, 0,
              dec_coords[DecTopRight].w, dec_coords[DecTopRight].h,
              w - dec_coords[DecTopRight].w, 0);

    XCopyArea(dpy, dec_tiles[tint][DecBottomLeft], dec_pm, gc,
              0, 0,
              dec_coords[DecBottomLeft].w, dec_coords[DecBottomLeft].h,
              0, h - dec_coords[DecBottomLeft].h);

    XCopyArea(dpy, dec_tiles[tint][DecBottomRight], dec_pm, gc,
              0, 0,
              dec_coords[DecBottomRight].w, dec_coords[DecBottomRight].h,
              w - dec_coords[DecBottomRight].w, h - dec_coords[DecBottomLeft].h);

    if (dec_has_title)
    {
        x = dec_title.left_offset;
        y = dec_title.baseline_top_offset;
        w = (dgeo.left_width + c->w + dgeo.right_width) - dec_title.left_offset
            - dec_title.right_offset;
        draw_text(dec_pm, font[FontTitle], &font_color[tint], x, y, w, c->title);
    }

    /* Pixmap drawing complete, now copy those areas onto the windows */

    if (which == DecWinTop || which == DecWinLAST)
        XCopyArea(dpy, dec_pm, c->decwin[DecWinTop], gc,
                  0, 0,
                  dgeo.left_width + c->w + dgeo.right_width, dgeo.top_height,
                  0, 0);

    if (which == DecWinLeft || which == DecWinLAST)
        XCopyArea(dpy, dec_pm, c->decwin[DecWinLeft], gc,
                  0, dgeo.top_height,
                  dgeo.left_width, c->h,
                  0, 0);

    if (which == DecWinRight || which == DecWinLAST)
        XCopyArea(dpy, dec_pm, c->decwin[DecWinRight], gc,
                  dgeo.left_width + c->w, dgeo.top_height,
                  dgeo.right_width, c->h,
                  0, 0);

    if (which == DecWinBottom || which == DecWinLAST)
        XCopyArea(dpy, dec_pm, c->decwin[DecWinBottom], gc,
                  0, dgeo.top_height + c->h,
                  dgeo.left_width + c->w + dgeo.right_width, dgeo.bottom_height,
                  0, 0);

    XFreePixmap(dpy, dec_pm);
    XFreeGC(dpy, gc);
    XFreeGC(dpy, gc_tiled);
}

Pixmap
decorations_get_pm(GC gc, XImage **ximg, enum DecorationLocation l,
                   enum DecTint t)
{
    Pixmap p;

    p = XCreatePixmap(dpy, root, dec_coords[l].w, dec_coords[l].h,
                      DefaultDepth(dpy, screen));
    XPutImage(dpy, p, gc, ximg[t],
              dec_coords[l].x, dec_coords[l].y,
              0, 0,
              dec_coords[l].w, dec_coords[l].h);

    return p;
}

void
decorations_load(void)
{
    char *tinted[DecTintLAST];
    XImage *ximg[DecTintLAST];
    size_t i, j;
    GC gc;

    /* The source image of our decorations is grey scale, but it's
     * already 24 bits. This allows us to easily tint the image. Then,
     * an XImage will be created for each tinted source image.
     *
     * XImages live in client memory, so we can pass xlib a pointer to
     * our data. XImages can then be copied to Pixmaps which live on the
     * server. Later on, we will only use those Pixmaps to draw
     * decorations. */

    for (i = DecTintNormal; i <= DecTintUrgent; i++)
    {
        tinted[i] = decorations_tint(dec_tints[i]);
        ximg[i] = decorations_to_ximg(tinted[i]);
    }

    gc = XCreateGC(dpy, root, 0, NULL);
    for (i = DecTintNormal; i <= DecTintUrgent; i++)
        for (j = DecTopLeft; j <= DecBottomRight; j++)
            dec_tiles[i][j] = decorations_get_pm(gc, ximg, j, i);
    XFreeGC(dpy, gc);

    for (i = DecTintNormal; i <= DecTintUrgent; i++)
        /* Note: This also frees tinted[i] */
        XDestroyImage(ximg[i]);
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
        /* r = original_r * tint / 255, i.e. a pixel with value 255 in
         * the source image will have the full tint color, pixels with
         * less than 255 will dim the tint color */

        r = (0x00FF0000 & dec_img[i]) >> 16;
        g = (0x0000FF00 & dec_img[i]) >> 8;
        b = (0x000000FF & dec_img[i]);

        r *= tr;
        g *= tg;
        b *= tb;

        r /= 255;
        g /= 255;
        b /= 255;

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
draw_text(Drawable d, XftFont *xfont, XftColor *col, int x, int y, int w, char *s)
{
    XftDraw *xd;
    XGlyphInfo ext;
    int len;

    /* Just a quick sanity check */
    if (strlen(s) > 4096)
        return;

    /* Reduce length until the rendered text can fit into the desired
     * width -- this is "kind of"-ish UTF-8 compatible */
    for (len = strlen(s); len >= 0; len--)
    {
        XftTextExtentsUtf8(dpy, xfont, (XftChar8 *)s, len, &ext);
        if (ext.xOff < w)
            break;
    }

    if (len == 0)
        return;

    xd = XftDrawCreate(dpy, d, DefaultVisual(dpy, screen),
                       DefaultColormap(dpy, screen));
    XftDrawStringUtf8(xd, col, xfont, x, y, (XftChar8 *)s, len);
    XftDrawDestroy(xd);
}

void
handle_clientmessage(XEvent *e)
{
    XClientMessageEvent *cme = &e->xclient;
    enum IPCCommand cmd;
    char arg, *an;
    struct Client *c;

    /* All sorts of client messages arrive here, including our own IPC
     * mechanism */

    if (cme->message_type == atom_ipc)
    {
        cmd = (enum IPCCommand)cme->data.b[0];
        arg = (char)cme->data.b[1];

        if (ipc_handler[cmd])
            ipc_handler[cmd](arg);
    }
    else if (cme->message_type == atom_net[AtomNetWMState])
    {
        if ((c = client_get_for_window(cme->window)) == NULL)
        {
            DPRINTF(__NAME_WM__": Window %lu sent EWMH message, but we don't "
                    "manage this window\n", cme->window);
        }

        if (c)
        {
            /* An EWMH client state message might contain two properties
             * to alter, data.l[1] and data.l[2] */
            if ((Atom)cme->data.l[1] == atom_net[AtomNetWMStateFullscreen]
                || (Atom)cme->data.l[2] == atom_net[AtomNetWMStateFullscreen])
            {
                DPRINTF(__NAME_WM__": Client %p requested EWMH fullscreen\n",
                        (void *)c);

                /* 0 = remove, 1 = add, 2 = toggle */
                if (c->fullscreen && (cme->data.l[0] == 0
                                      || cme->data.l[0] == 2))
                {
                    manage_fullscreen(c, 0);
                }
                else if (!c->fullscreen && (cme->data.l[0] == 1
                                            || cme->data.l[0] == 2))
                {
                    manage_fullscreen(c, 1);
                }
            }
            else
            {
                an = XGetAtomName(dpy, cme->message_type);
                DPRINTF(__NAME_WM__": Received EWMH message with unknown "
                        "action: %lu, %s\n", cme->data.l[0], an ? an : "(nil)");
                if (an)
                    XFree(an);
            }
        }
    }
    else
    {
        an = XGetAtomName(dpy, cme->message_type);
        DPRINTF(__NAME_WM__": Received client message with unknown type: %lu"
                ", %s\n", cme->message_type, an ? an : "(nil)");
        if (an)
            XFree(an);
    }
}

void
handle_configurenotify(XEvent *e)
{
    XConfigureEvent *ev = &e->xconfigure;
    struct Client *c;

    if (ev->window != root)
        return;

    /* screen_w and screen_h don't really matter to us. We use XRandR to
     * detect monitors. However, sometimes there are multiple identical
     * ConfigureNotify events. In order to avoid unnecessary
     * reconfiguration on our part, we try to filter those. */
    if (ev->width == screen_w && ev->height == screen_h)
        return;

    DPRINTF(__NAME_WM__": ConfigureNotify received, reconfiguring monitors \n");

    shutdown_monitors_free();
    setup_monitors_read();

    for (c = clients; c; c = c->next)
        c->mon = selmon;

    screen_w = ev->width;
    screen_h = ev->height;

    /* Hide everything, then unhide what should be visible on the
     * default workspace */
    for (c = clients; c; c = c->next)
        manage_showhide(c, 1);

    for (c = clients; c; c = c->next)
        if (c->mon == selmon && c->workspace == selmon->active_workspace)
            manage_showhide(c, 0);

    manage_arrange(selmon);
    manage_raisefocus_first_matching();
}

void
handle_configurerequest(XEvent *e)
{
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    XConfigureEvent ce;
    XWindowChanges wc;
    struct Client *c = NULL;

    if ((c = client_get_for_window(ev->window)))
    {
        /* This is a known client. However, we do not allow the client
         * to resize or move itself. Hence, we simply inform him about
         * the last known configuration. */
        ce.type = ConfigureNotify;
        ce.display = dpy;
        ce.event = c->win;
        ce.window = c->win;
        ce.x = c->x;
        ce.y = c->y;
        ce.width = c->w;
        ce.height = c->h;
        ce.border_width = 0;
        ce.above = None;
        ce.override_redirect = False;
        XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
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
    {
        unmanage(c);
        manage_client_gone(c);
    }
}

void
handle_expose(XEvent *e)
{
    XExposeEvent *ev = &e->xexpose;
    struct Client *c;
    enum DecorationWindowLocation which;

    if ((c = client_get_for_decoration(ev->window, &which)) == NULL)
        return;

    decorations_draw_for_client(c, which);
}

void
handle_focusin(XEvent *e)
{
    XFocusChangeEvent *ev = &e->xfocus;

    /* Some clients try to grab focus without being the selected window.
     * We don't want that. Unfortunately, there is no way to *prevent*
     * this. The only thing we can do, is to revert the focus to what we
     * think should have it. */

    if (selc && ev->window != selc->win)
        manage_xfocus(selc);
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
handle_propertynotify(XEvent *e)
{
    XWMHints *wmh;
    XPropertyEvent *ev = &e->xproperty;
    struct Client *c;
    char *an = NULL;

    if ((c = client_get_for_window(ev->window)) == NULL)
        return;

    if (ev->state != PropertyDelete)
    {
        if (ev->atom == atom_net[AtomNetWMName] || ev->atom == XA_WM_NAME)
        {
            client_update_title(c);
            decorations_draw_for_client(c, DecWinLAST);
        }
        else if (ev->atom == XA_WM_HINTS)
        {
            if ((wmh = XGetWMHints(dpy, c->win)))
            {
                if (wmh->flags & XUrgencyHint)
                {
                    if (c == selc && VIS_ON_SELMON(c))
                    {
                        /* Setting the urgency hint on the currently
                         * selected window shall have no effect */
                        wmh->flags &= ~XUrgencyHint;
                        XSetWMHints(dpy, c->win, wmh);
                        DPRINTF(__NAME_WM__": Urgency hint on client %p "
                                "ignored because selected\n", (void *)c);
                    }
                    else
                    {
                        c->urgent = 1;
                        decorations_draw_for_client(c, DecWinLAST);
                        DPRINTF(__NAME_WM__": Urgency hint on client %p set\n",
                                (void *)c);
                    }
                }
                else if (c->urgent)
                {
                    /* Urgency hint has been cleared by the application */
                    c->urgent = 0;
                    decorations_draw_for_client(c, DecWinLAST);
                    DPRINTF(__NAME_WM__": Urgency hint on client %p cleared\n",
                            (void *)c);
                }
                publish_state();
                XFree(wmh);
            }
        }
        /* XXX ev->atom == XA_WM_TRANSIENT_FOR
         * dwm indicates that there might be changes to the
         * transient_for_hint after the window has been mapped. I'm not
         * sure if we really need this. I couldn't find anything in
         * ICCCM that indicates that this could be happening. Maybe
         * broken clients behave this way, though. */
        else
        {
            an = XGetAtomName(dpy, ev->atom);
            DPRINTF(__NAME_WM__": PropertyNotify about unhandled atom '%s'\n",
                    an ? an : "(nil)");
            if (an)
                XFree(an);
        }
    }
}

void
handle_unmapnotify(XEvent *e)
{
    XUnmapEvent *ev = &e->xunmap;
    struct Client *c;

    if ((c = client_get_for_window(ev->window)))
    {
        unmanage(c);
        manage_client_gone(c);
    }
}

void
ipc_client_close(char arg)
{
    XEvent ev;

    (void)arg;

    if (!SOMETHING_FOCUSED)
        return;

    memset(&ev, 0, sizeof ev);
    ev.xclient.type = ClientMessage;
    ev.xclient.window = selc->win;
    ev.xclient.message_type = atom_wm[AtomWMProtocols];
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = atom_wm[AtomWMDeleteWindow];
    ev.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, selc->win, False, NoEventMask, &ev);
}

void
ipc_client_kill(char arg)
{
    (void)arg;

    if (!SOMETHING_FOCUSED)
        return;

    XKillClient(dpy, selc->win);
}

void
ipc_client_fullscreen_toggle(char arg)
{
    (void)arg;

    if (!SOMETHING_FOCUSED)
        return;

    manage_fullscreen(selc, !selc->fullscreen);
}

void
ipc_client_move_list(char arg)
{
    char use_next = 0;
    struct Client *c, *one = NULL, *two = NULL,
                  *before_one = NULL, *before_two = NULL;

    if (!SOMETHING_FOCUSED)
        return;

    if (selc->fullscreen)
        return;

    if (arg < 0)
    {
        /* Find visible client before selc */
        for (c = clients; c; c = c->next)
        {
            if (VIS_ON_SELMON(c))
            {
                if (c == selc)
                    break;
                else
                {
                    /* List order: "one" precedes "two" */
                    one = c;
                    two = selc;
                }
            }
        }
    }
    else
    {
        /* Find visible client after selc */
        for (c = selc; c; c = c->next)
        {
            if (VIS_ON_SELMON(c))
            {
                if (c == selc)
                    use_next = 1;
                else if (use_next)
                {
                    one = selc;
                    two = c;
                    break;
                }
            }
        }
    }

    DPRINTF(__NAME_WM__": Found partners: %p, %p\n", (void *)one, (void *)two);

    if (one == NULL || two == NULL)
        return;

    /* The following method does not really "swap" the two list
     * elements. It simply removes the second item from the list and
     * then prepends it in front of the first item. Thus, "two" now
     * comes before "one". */

    for (c = clients; c && c->next != two; c = c->next)
        /* nop */;
    before_two = c;

    /* Remove "two" from the list */
    before_two->next = two->next;

    if (one == clients)
    {
        /* "one" is the very first client, so we can simply put "two" at
         * the list head */
        two->next = clients;
        clients = two;
    }
    else
    {
        /* "one" is not the list head, so we have to find the item
         * before it and then insert "two" at that location */

        for (c = clients; c && c->next != one; c = c->next)
            /* nop */;
        before_one = c;

        before_one->next = two;
        two->next = one;
    }

    manage_arrange(selmon);
}

void
ipc_client_move_mouse(char arg)
{
    int x, y, di, dx, dy;
    unsigned int dui;
    Window child, dummy;
    struct Client *c;

    /* We have no native means of handling mouse input, we merely handle
     * an IPC request. This means that we will only query the current
     * pointer location, plus the window that's below the pointer. No
     * grabbing of keys, buttons, or pointers is involved. */

    XQueryPointer(dpy, root, &dummy, &child, &x, &y, &di, &di, &dui);

    if (arg == 0)
    {
        DPRINTF(__NAME_WM__": Mouse move: down at %d, %d over %lu\n",
                x, y, child);

        mouse_dc = NULL;

        if ((c = client_get_for_window(child))
            || (c = client_get_for_decoration(child, NULL)))
        {
            if (!c->fullscreen)
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
ipc_client_resize_mouse(char arg)
{
    /* TODO lots of code duplication from ipc_client_move_mouse() */

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

        if ((c = client_get_for_window(child))
            || (c = client_get_for_decoration(child, NULL)))
        {
            if (!c->fullscreen)
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
ipc_client_select_adjacent(char arg)
{
    struct Client *c, *to_select = NULL;

    /* Select the previous/next client which is visible, wrapping
     * around. Once again, props to dwm. */

    if (!SOMETHING_FOCUSED)
        return;

    if (arg > 0)
    {
        /* Start at selc and search the next visible client. If that
         * doesn't work, then start from the beginning of the list. */

        for (c = selc->next; c && !VIS_ON_SELMON(c); c = c->next)
            /* nop */;

        if (!c)
            for (c = clients; c && !VIS_ON_SELMON(c); c = c->next)
                /* nop */;

        to_select = c;
    }
    else if (arg < 0)
    {
        /* Look at all visible clients before c, keep track of c and the
         * visible client before it (to_select). Once we have found
         * selc, we will not enter the loop body, thus to_select will
         * still point to the visible client before c. */
        for (c = clients; c != selc; c = c->next)
            if (VIS_ON_SELMON(c))
                to_select = c;

        /* Nothing found? Then start at selc and look at all clients
         * after c. After this loop, to_select will point to the last
         * visible client in the list. */
        if (!to_select)
            for (c = selc->next; c; c = c->next)
                if (VIS_ON_SELMON(c))
                    to_select = c;
    }

    if (to_select)
        manage_raisefocus(to_select);
}

void
ipc_client_select_recent(char arg)
{
    struct Client *c;

    (void)arg;

    /* In the focus list, we must find the second matching client for
     * the current workspace/monitor. By definition, the first matching
     * client is selc itself, so we can easily skip it. */

    for (c = selc->focus_next; c; c = c->focus_next)
        if (VIS_ON_SELMON(c))
            break;

    if (c)
        manage_raisefocus(c);
}

void
ipc_client_switch_monitor_adjacent(char arg)
{
    int i;
    struct Monitor *m, *old_mon = NULL;

    /* Move currently selected client to an adjacent monitor, causing
     * both monitors to be re-arranged */

    if (!SOMETHING_FOCUSED)
        return;

    if (selc->fullscreen)
        return;

    i = selmon->index;
    i += arg;
    i = i < 0 ? 0 : i;
    i = i >= monitors_num ? monitors_num - 1 : i;

    for (m = monitors; m; m = m->next)
    {
        if (m->index == i)
        {
            old_mon = selc->mon;
            selc->mon = m;
            selc->workspace = selc->mon->active_workspace;

            /* If the client is floating or the target layout is
             * floating, then we need to re-fit the client and apply the
             * newly calculated size. This has no effect for
             * non-floaters because we call manage_arrange() afterwards. */
            manage_fit_on_monitor(selc);
            manage_setsize(selc);

            manage_arrange(old_mon);
            manage_arrange(m);
            manage_raisefocus_first_matching();
            return;
        }
    }
}

void
ipc_client_switch_workspace(char arg)
{
    int i;

    if (!SOMETHING_FOCUSED)
        return;

    if (selc->fullscreen)
        return;

    i = arg;
    i = i < WORKSPACE_MIN ? WORKSPACE_MIN : i;
    i = i > WORKSPACE_MAX ? WORKSPACE_MAX : i;

    selc->workspace = i;
    manage_goto_workspace(selmon->active_workspace);
}

void
ipc_client_switch_workspace_adjacent(char arg)
{
    int i;

    if (!SOMETHING_FOCUSED)
        return;

    if (selc->fullscreen)
        return;

    i = selmon->active_workspace + arg;
    i = i < WORKSPACE_MIN ? WORKSPACE_MIN : i;
    i = i > WORKSPACE_MAX ? WORKSPACE_MAX : i;

    selc->workspace = i;
    manage_goto_workspace(selmon->active_workspace);
}

void
ipc_layout_set(char arg)
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
ipc_monitor_select_adjacent(char arg)
{
    int i;

    i = selmon->index;
    i += arg;
    i = i < 0 ? 0 : i;
    i = i >= monitors_num ? monitors_num - 1 : i;

    manage_goto_monitor(i);
}

void
ipc_monitor_select_recent(char arg)
{
    (void)arg;

    manage_goto_monitor(prevmon_i);
}

void
ipc_wm_quit(char arg)
{
    (void)arg;

    running = 0;

    DPRINTF(__NAME_WM__": Quitting\n");
}

void
ipc_wm_restart(char arg)
{
    (void)arg;

    restart = 1;
    running = 0;

    DPRINTF(__NAME_WM__": Quitting for restart\n");
}

void
ipc_workspace_select_adjacent(char arg)
{
    int i;

    i = selmon->active_workspace;
    i += arg;
    manage_goto_workspace(i);
}

void
ipc_workspace_select_recent(char arg)
{
    (void)arg;

    manage_goto_workspace(selmon->recent_workspace);
}

void
ipc_workspace_select(char arg)
{
    int i;

    i = arg;
    manage_goto_workspace(i);
}

void
layout_float(struct Monitor *m)
{
    (void)m;
}

void
layout_monocle(struct Monitor *m)
{
    struct Client *c;

    for (c = clients; c; c = c->next)
    {
        if (VIS_ON_M(c, m) && !c->floating && !c->fullscreen)
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
    int num_clients = 0, at_y, slave_h, master_w;

    /* Note: at_y, slave_h and master_w all the *visible* sizes
     * including decorations */

    at_y = m->wy;

    for (c = clients; c; c = c->next)
        if (VIS_ON_M(c, m) && !c->floating && !c->fullscreen)
            num_clients++;

    for (c = clients; c; c = c->next)
    {
        if (VIS_ON_M(c, m) && !c->floating && !c->fullscreen)
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
                    slave_h = c->mon->wh - at_y + m->wy;
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
    struct Client *c, *tc;
    Window transient_for;
    Atom prop, da;
    unsigned char *prop_ret = NULL;
    int di;
    unsigned long dl;
    char *an;

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

    client_update_title(c);
    XSelectInput(dpy, c->win, 0
                 /* FocusIn, FocusOut */
                 | FocusChangeMask
                 /* All kinds of properties, window titles, EWMH, ... */
                 | PropertyChangeMask
                 );

    decorations_create(c);

    if (XGetTransientForHint(dpy, c->win, &transient_for))
    {
        /* ICCCM says: "The WM_TRANSIENT_FOR property (of type WINDOW)
         * contains the ID of another top-level window. The implication
         * is that this window is a pop-up on behalf of the named
         * window [...]"
         *
         * A popup window should always be floating. */
        c->floating = 1;

        /* Try to find the other client this window is transient for. If
         * we don't find it, root and None are valid values, too
         * (according to EWMH), meaning this window should be treated as
         * "transient for all other windows in this group". However, we
         * don't have to do anything special in these cases. */
        if ((tc = client_get_for_window(transient_for)))
        {
            c->mon = tc->mon;
            c->workspace = tc->workspace;
        }
    }

    if (XGetWindowProperty(dpy, c->win, atom_net[AtomNetWMWindowType], 0,
                           sizeof (Atom), False, XA_ATOM, &da, &di, &dl, &dl,
                           &prop_ret)
        == Success)
    {
        if (prop_ret)
        {
            prop = ((Atom *)prop_ret)[0];
            if (prop == atom_net[AtomNetWMWindowTypeDialog]
                || prop == atom_net[AtomNetWMWindowTypeMenu]
                || prop == atom_net[AtomNetWMWindowTypeSplash]
                || prop == atom_net[AtomNetWMWindowTypeToolbar]
                || prop == atom_net[AtomNetWMWindowTypeUtility])
            {
                c->floating = 1;
                an = XGetAtomName(dpy, prop);
                DPRINTF(__NAME_WM__": Client %p should be floating, says EWMH"
                        " (has type %s)\n", (void *)c, an ? an : "(nil)");
                if (an)
                    XFree(an);
            }
            else
                DPRINTF(__NAME_WM__": Client %p has EWMH type, but we don't "
                        "know that type\n", (void *)c);
        }
        else
            DPRINTF(__NAME_WM__": Client %p has EWMH type, but pointer NULL\n",
                    (void *)c);
    }
    else
        DPRINTF(__NAME_WM__": Client %p has no EWMH window type\n", (void *)c);

    DPRINTF(__NAME_WM__": Client %p lives on WS %d on monitor %d\n", (void *)c,
            c->workspace, c->mon->index);

    manage_fit_on_monitor(c);
    manage_setsize(c);

    client_save(c);

    DPRINTF(__NAME_WM__": Managing window %lu (%p) at %dx%d+%d+%d\n",
            c->win, (void *)c, c->w, c->h, c->x, c->y);

    /* XXX why arrange after mapping the window? */
    XMapWindow(dpy, c->win);
    manage_arrange(c->mon);
    manage_raisefocus(c);
}

void
manage_arrange(struct Monitor *m)
{
    DPRINTF(__NAME_WM__": Arranging monitor %p\n", (void *)m);
    layouts[m->layouts[m->active_workspace]](m);

    publish_state();
}

void
manage_client_gone(struct Client *c)
{
    struct Client *old_selc;

    DPRINTF(__NAME_WM__": Client %p gone\n", (void *)c);

    old_selc = selc;

    manage_arrange(c->mon);
    manage_focus_remove(c);

    /* If c was the focused/selected client (this implies "selmon ==
     * c->mon"), then we have to select a new client: We choose the
     * first matching client in the focus list -- "matching" means it's
     * the correct workspace and monitor */
    if (c == old_selc)
        manage_raisefocus_first_matching();

    publish_state();
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

    if (c->fullscreen)
        return;

    if (c->x - dgeo.left_width < c->mon->wx)
        c->x = c->mon->wx + dgeo.left_width;
    if (c->y - dgeo.top_height < c->mon->wy)
        c->y = c->mon->wy + dgeo.top_height;
    if (c->x + c->w + dgeo.right_width >= c->mon->wx + c->mon->ww)
        c->x = c->mon->wx + c->mon->ww - c->w - dgeo.right_width;
    if (c->y + c->h + dgeo.bottom_height >= c->mon->wy + c->mon->wh)
        c->y = c->mon->wy + c->mon->wh - c->h - dgeo.bottom_height;

    /* When a window spawns "in the background", we put into hidden
     * state. Caution: This function is also called by
     * ipc_client_switch_monitor_adjacent(), not only by manage(). */
    if (c->workspace != c->mon->active_workspace)
    {
        DPRINTF(__NAME_WM__": Client %p spawned in background, hiding\n",
                (void *)c);
        manage_showhide(c, 1);
    }
}

void
manage_focus_add(struct Client *new_selc)
{
    /* Add client to head of the focus list */

    new_selc->focus_next = selc;
    selc = new_selc;
}

void
manage_focus_remove(struct Client *new_selc)
{
    char found = 0;
    struct Client **tc, *c;

    /* Remove client from focus list (if present) */

    for (c = selc; !found && c; c = c->focus_next)
        if (c == new_selc)
            found = 1;

    if (!found)
        return;

    for (tc = &selc; *tc && *tc != new_selc; tc = &(*tc)->focus_next);
    *tc = new_selc->focus_next;
}

void
manage_focus_set(struct Client *new_selc)
{
    struct Client *c, *old_selc;

    DPRINTF(__NAME_WM__": selc before list manipulation: %p (%p)\n",
            (void *)selc, (void *)(selc ? selc->focus_next : NULL));
    DPRINTF(__NAME_WM__": new_selc before list manipulation: %p (%p)\n",
            (void *)new_selc, (void *)(new_selc ? new_selc->focus_next : NULL));

    old_selc = selc;

    if (new_selc)
    {
        /* Move newly selected client to head of focus list, thus
         * changing selc */

        DPRINTF(__NAME_WM__": Focus list (pre remove): ");
        for (c = selc; c; c = c->focus_next)
            DPRINTF("%p (%p) ", (void *)c, (void *)(c ? c->focus_next : NULL));
        DPRINTF("\n");

        manage_focus_remove(new_selc);

        DPRINTF(__NAME_WM__": Focus list (pre add): ");
        for (c = selc; c; c = c->focus_next)
            DPRINTF("%p (%p) ", (void *)c, (void *)(c ? c->focus_next : NULL));
        DPRINTF("\n");

        manage_focus_add(new_selc);
    }

    DPRINTF(__NAME_WM__": Focus list: ");
    for (c = selc; c; c = c->focus_next)
        DPRINTF("%p (%p) ", (void *)c, (void *)(c ? c->focus_next : NULL));
    DPRINTF("\n");

    /* Unfocus previous client, focus new client */
    if (old_selc)
        decorations_draw_for_client(old_selc, DecWinLAST);

    if (new_selc)
        decorations_draw_for_client(new_selc, DecWinLAST);
}

void
manage_fullscreen(struct Client *c, char fs)
{
    if (fs)
    {
        c->fullscreen = 1;

        c->normal_x = c->x;
        c->normal_y = c->y;
        c->normal_w = c->w;
        c->normal_h = c->h;

        c->x = c->mon->mx;
        c->y = c->mon->my;
        c->w = c->mon->mw;
        c->h = c->mon->mh;

        /* We only support the state "fullscreen", so it's okay-ish to
         * only ever set this property (and kill all others) */
        XChangeProperty(dpy, c->win, atom_net[AtomNetWMState], XA_ATOM,
                        32, PropModeReplace,
                        (unsigned char *)&atom_net[AtomNetWMStateFullscreen], 1);

        manage_setsize(c);
    }
    else
    {
        c->fullscreen = 0;

        c->x = c->normal_x;
        c->y = c->normal_y;
        c->w = c->normal_w;
        c->h = c->normal_h;

        /* XXX dwm empties the list instead of simply removing the
         * property. Why is that? */
        XDeleteProperty(dpy, c->win, atom_net[AtomNetWMState]);

        manage_setsize(c);
    }
}

void
manage_goto_monitor(int i)
{
    struct Monitor *m, *new_selmon = NULL;

    for (m = monitors; m; m = m->next)
    {
        if (m->index == i)
        {
            new_selmon = m;
            break;
        }
    }

    if (new_selmon == NULL)
        return;

    prevmon_i = selmon->index;
    selmon = new_selmon;

    XWarpPointer(dpy, None, root, 0, 0, 0, 0,
                 selmon->wx + selmon->ww / 2, selmon->wy + selmon->wh / 2);
    manage_raisefocus_first_matching();
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

    selmon->recent_workspace = selmon->active_workspace;
    selmon->active_workspace = i;

    manage_arrange(selmon);
    manage_raisefocus_first_matching();
}

void
manage_raisefocus(struct Client *c)
{
    if (c && !VIS_ON_SELMON(c))
    {
        DPRINTF(__NAME_WM__": Client %p should have been focused/raised, "
                "but it's not currently visible. Ignoring.\n", (void *)c);
        return;
    }

    manage_xraise(c);
    manage_xfocus(c);
    manage_focus_set(c);
}

void
manage_raisefocus_first_matching(void)
{
    struct Client *c;

    for (c = selc; c; c = c->focus_next)
    {
        if (VIS_ON_SELMON(c))
        {
            manage_raisefocus(c);
            return;
        }
    }

    /* If we end up here, then no client has been found on the target
     * monitor. Still, due to the fact that we changed the monitor, we
     * must now unfocus the previously selected client. */
    manage_raisefocus(NULL);
}

void
manage_showhide(struct Client *c, char hide)
{
    if (hide && !c->hidden)
    {
        c->nonhidden_x = c->x;
        c->x = -2 * c->w;
        c->hidden = 1;

        manage_setsize(c);
    }

    if (!hide)
    {
        c->x = c->nonhidden_x;
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

    if (c->fullscreen && !c->hidden)
    {
        DPRINTF(__NAME_WM__": Fullscreening client %p\n", (void *)c);

        XMoveResizeWindow(dpy, c->decwin[DecWinTop], -15, 0, 10, 10);
        XMoveResizeWindow(dpy, c->decwin[DecWinLeft], -15, 0, 10, 10);
        XMoveResizeWindow(dpy, c->decwin[DecWinRight], -15, 0, 10, 10);
        XMoveResizeWindow(dpy, c->decwin[DecWinBottom], -15, 0, 10, 10);
    }
    else
    {
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
    }

    XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
}

void
manage_xfocus(struct Client *c)
{
    XWMHints *wmh;

    if (c)
    {
        c->urgent = 0;
        if ((wmh = XGetWMHints(dpy, c->win)))
        {
            wmh->flags &= ~XUrgencyHint;
            XSetWMHints(dpy, c->win, wmh);
            XFree(wmh);
        }
        publish_state();

        XSetInputFocus(dpy, c->win, RevertToParent, CurrentTime);
    }
    else
        XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
}

void
manage_xraise(struct Client *c)
{
    size_t i;

    if (!c)
        return;

    XRaiseWindow(dpy, c->win);

    for (i = DecWinTop; i <= DecWinBottom; i++)
        XRaiseWindow(dpy, c->decwin[i]);
}

void
publish_state(void)
{
    size_t size, off, byte_i, shifts_needed, i, size_monws;
    unsigned char *state = NULL, byte, mask;
    struct Monitor *m;
    struct Client *c;

    /* The very first byte indicates the number of monitors detected by
     * us. Then, the first monitors_num bytes indicate the active
     * workspace on each monitor. The next monitors_num bytes indicate
     * the active layout on each monitor (note: different layouts might
     * be active on different workspaces on each monitor, but they are
     * not visible anyway, so they're not included). Following that, we
     * need WORKSPACE_MAX / 8 = ~16 bytes per monitor to indicate
     * whether that workspace is occupied. We need the same amount of
     * data to indicate whether a workspace has the urgency hint set. */

    size_monws = 16;

    size = 1 + monitors_num * 2 + monitors_num * size_monws * 2;
    state = calloc(size, sizeof (unsigned char));
    if (state == NULL)
    {
        fprintf(stderr, __NAME_WM__": Could not allocate memory for state array\n");
        return;
    }

    /* Number of detected monitors (int) */
    state[0] = monitors_num;
    off = 1;

    /* Active workspace on each monitor (int) */
    for (m = monitors; m; m = m->next)
        state[off + m->index] = m->active_workspace;
    off += monitors_num;

    /* Visible layout on each monitor (layout index as int) */
    for (m = monitors; m; m = m->next)
        state[off + m->index] = m->layouts[m->active_workspace];
    off += monitors_num;

    /* Bitmasks for occupied workspaces and urgent hints */
    for (c = clients; c; c = c->next)
    {
        /* Calculate which byte to alter and then which bit to set */

        byte_i = (c->workspace - 1) / 8;
        shifts_needed = (c->workspace - 1) % 8;

        mask = 1;
        for (i = 0; i < shifts_needed; i++)
            mask <<= 1;

        /* Occupied workspaces */
        i = off + c->mon->index * size_monws + byte_i;
        byte = state[i];
        byte |= mask;
        state[i] = byte;

        /* Urgency hints */
        if (c->urgent)
        {
            i += monitors_num * size_monws;
            byte = state[i];
            byte |= mask;
            state[i] = byte;
        }
    }
    off += 2 * (monitors_num * size_monws);

    XChangeProperty(dpy, root, atom_state, XA_INTEGER, 8, PropModeReplace,
                    state, size);
    DPRINTF(__NAME_WM__": Published internal state in root property %s: ",
            IPC_ATOM_STATE);
    for (i = 0; i < size; i++)
        DPRINTF("%d ", state[i]);
    DPRINTF("\n");
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
    size_t i;

    if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
        fprintf(stderr, __NAME_WM__": Could not set locale\n");

    if ((dpy = XOpenDisplay(NULL)) == NULL)
    {
        fprintf(stderr, __NAME_WM__": Cannot open display\n");
        exit(EXIT_FAILURE);
    }
    root = DefaultRootWindow(dpy);
    screen = DefaultScreen(dpy);
    xerrorxlib = XSetErrorHandler(xerror);

    setup_hints();
    atom_ipc = XInternAtom(dpy, IPC_ATOM_COMMAND, False);
    atom_state = XInternAtom(dpy, IPC_ATOM_STATE, False);

    /* Initialize fonts and colors
     *
     * (Yes, looping over that one item in the font array is
     * meaningless. Today. In the future, there might be more than one
     * font.) */
    for (i = FontTitle; i <= FontTitle; i++)
    {
        font[i] = XftFontOpenName(dpy, screen, dec_fonts[i]);
        if (!font[i])
        {
            fprintf(stderr, __NAME_WM__": Cannot open font '%s'\n", dec_fonts[i]);
            exit(EXIT_FAILURE);
        }
        DPRINTF(__NAME_WM__": Loaded font '%s'\n", dec_fonts[i]);
    }
    for (i = DecTintNormal; i <= DecTintUrgent; i++)
    {
        if (!XftColorAllocName(dpy, DefaultVisual(dpy, screen),
                               DefaultColormap(dpy, screen), dec_font_colors[i],
                               &font_color[i]))
        {
            fprintf(stderr, __NAME_WM__": Cannot load color '%s'\n",
                    dec_font_colors[i]);
            exit(EXIT_FAILURE);
        }
        DPRINTF(__NAME_WM__": Loaded color '%s'\n", dec_font_colors[i]);
    }

    setup_monitors_read();
    decorations_load();

    XSelectInput(dpy, root, 0
                 /* RandR protocol says: "Clients MAY select for
                  * ConfigureNotify on the root window to be
                  * informed of screen changes." Selecting for
                  * StructureNotifyMask creates such events. */
                 | StructureNotifyMask
                 /* Manage creation and destruction of windows.
                  * SubstructureRedirectMask is also used by our IPC
                  * client and possibly EWMH clients, both sending us
                  * ClientMessages. */
                 | SubstructureRedirectMask | SubstructureNotifyMask
                 );

    /* Set default cursor on root window */
    cursor_normal = XCreateFontCursor(dpy, XC_left_ptr);
    XDefineCursor(dpy, root, cursor_normal);

    publish_state();
}

void
setup_hints(void)
{
    atom_net[AtomNetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
    atom_net[AtomNetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
    atom_net[AtomNetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
    atom_net[AtomNetWMStateFullscreen] = XInternAtom(dpy,
                                                     "_NET_WM_STATE_FULLSCREEN",
                                                     False);

    atom_net[AtomNetWMWindowType] = XInternAtom(
            dpy,
            "_NET_WM_WINDOW_TYPE",
            False
    );
    atom_net[AtomNetWMWindowTypeDialog] = XInternAtom(
            dpy,
            "_NET_WM_WINDOW_TYPE_DIALOG",
            False
    );
    atom_net[AtomNetWMWindowTypeMenu] = XInternAtom(
            dpy,
            "_NET_WM_WINDOW_TYPE_MENU",
            False
    );
    atom_net[AtomNetWMWindowTypeSplash] = XInternAtom(
            dpy,
            "_NET_WM_WINDOW_TYPE_SPLASH",
            False
    );
    atom_net[AtomNetWMWindowTypeToolbar] = XInternAtom(
            dpy,
            "_NET_WM_WINDOW_TYPE_TOOLBAR",
            False
    );
    atom_net[AtomNetWMWindowTypeUtility] = XInternAtom(
            dpy,
            "_NET_WM_WINDOW_TYPE_UTILITY",
            False
    );

    XChangeProperty(dpy, root, atom_net[AtomNetSupported], XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)atom_net, AtomNetLAST);

    atom_wm[AtomWMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
    atom_wm[AtomWMDeleteWindow] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
}

void
setup_monitors_read(void)
{
    XRRCrtcInfo *ci;
    XRRScreenResources *sr;
    struct Monitor *m;
    int c, cinner;
    int minx, minindex;
    char *chosen = NULL;

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

        m = calloc(1, sizeof (struct Monitor));
        if (selmon == NULL)
            selmon = m;
        m->wx = m->mx = ci->x;
        m->wy = m->my = ci->y;
        m->ww = m->mw = ci->width;
        m->wh = m->mh = ci->height;

        m->wx += wai.left;
        m->ww -= wai.left + wai.right;
        m->wy += wai.top;
        m->wh -= wai.top + wai.bottom;

        m->index = monitors_num++;
        m->active_workspace = m->recent_workspace = 1;
        m->next = monitors;
        monitors = m;
        DPRINTF(__NAME_WM__": monitor: %d %d %d %d\n",
                ci->x, ci->y, ci->width, ci->height);
    }
    free(chosen);
}

void
scan(void)
{
    unsigned int i, num;
    Window d1, d2, *wins = NULL;
    XWindowAttributes wa;

    /* TODO grab the server while doing this. */

    /* TODO We ignore iconified windows for now. */

    if (XQueryTree(dpy, root, &d1, &d2, &wins, &num))
    {
        DPRINTF(__NAME_WM__": scan() saw %d windows\n", num);

        /* First, manage all top-level windows. Then manage transient
         * windows. This is required because the windows pointed to by
         * "transient_for" must already be managed by us -- we copy some
         * attributes from the parents to their popups. */
        for (i = 0; i < num; i++)
        {
            if (!XGetWindowAttributes(dpy, wins[i], &wa) || wa.override_redirect)
                continue;
            if (XGetTransientForHint(dpy, wins[i], &d1))
                continue;
            if (wa.map_state == IsViewable)
                manage(wins[i], &wa);
        }
        for (i = 0; i < num; i++)
        {
            if (!XGetWindowAttributes(dpy, wins[i], &wa) || wa.override_redirect)
                continue;
            if (XGetTransientForHint(dpy, wins[i], &d1))
            {
                if (wa.map_state == IsViewable)
                    manage(wins[i], &wa);
            }
        }
        if (wins)
            XFree(wins);
    }
}

void
shutdown(void)
{
    size_t i, j;

    while (clients != NULL)
        unmanage(clients);

    for (i = DecTintNormal; i <= DecTintUrgent; i++)
        for (j = DecTopLeft; j <= DecBottomRight; j++)
            XFreePixmap(dpy, dec_tiles[i][j]);

    XDeleteProperty(dpy, root, atom_state);
    XDeleteProperty(dpy, root, atom_net[AtomNetSupported]);
    XFreeCursor(dpy, cursor_normal);

    XCloseDisplay(dpy);
}

void
shutdown_monitors_free(void)
{
    struct Monitor *m, *n;

    m = monitors;
    while (m)
    {
        n = m->next;
        free(m);
        m = n;
    }
    monitors = NULL;
    monitors_num = 0;
    selmon = NULL;
}

void
unmanage(struct Client *c)
{
    struct Client **tc;

    /* Remove client from "clients" list, props for this neat little
     * loop go to dwm */
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
