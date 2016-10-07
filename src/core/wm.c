#include <arpa/inet.h>
#include <assert.h>
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

#define SAVE_SLOTS 8
#define WM_NAME_UNKNOWN "<name unknown>"
#define VIS_ON_SELMON(c) ((c)->mon == selmon && \
                          (c)->workspace == monitors[selmon].active_workspace)
#define VIS_ON_M(c, m) ((c)->mon == (m) && \
                        (c)->workspace == monitors[(m)].active_workspace)
#define SOMEHOW_FLOATING(c) ((c)->floating || \
                             monitors[(c)->mon].layouts[(c)->workspace] == LAFloat)
#define SOMETHING_FOCUSED (focus && VIS_ON_SELMON(focus))

struct Client
{
    Window win;
    Window decwin[DecWinLAST];

    /* Inner size of the actual client, excluding decorations */
    int x, y, w, h;
    int normal_x, normal_y, normal_w, normal_h;
    int nonhidden_x;

    /* Space to reserve for window decorations */
    int m_top, m_left, m_right, m_bottom;

    /* ICCCM 4.1.2.3 size hints */
    double sh_asp_min, sh_asp_max;
    int sh_base_w, sh_base_h, sh_inc_w, sh_inc_h;
    int sh_min_w, sh_min_h, sh_max_w, sh_max_h;

    bool floating;
    bool fullscreen;
    bool hidden;
    bool never_focus;
    bool undecorated;
    bool urgent;

    char title[512];

    int mon;
    int workspace;

    int saved_monitors[SAVE_SLOTS], saved_workspaces[SAVE_SLOTS];

    struct Client *next;
    struct Client *focus_next;
};

struct Monitor
{
    int active_workspace, recent_workspace;
    int layouts[WORKSPACE_MAX + 1];

    /* Actual monitor size */
    int mx, my, mw, mh;

    /* Logical size, i.e. where we can place windows */
    int wx, wy, ww, wh;
};

struct Rule
{
    char *class;
    char *instance;

    int workspace;
    int monitor;

    /* Note: Do not make this a bool. It has three states:
     * -1 = unchanged, 1 = force floating, 0 = force non-floating */
    char floating;
};

struct WorkareaInsets
{
    int top, left, right, bottom;
};

enum AtomsNet
{
    AtomNetSupported,
    AtomNetSupportingWMCheck,
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
    AtomWMDeleteWindow,
    AtomWMProtocols,
    AtomWMState,
    AtomWMTakeFocus,

    AtomWMLAST,
};

#include "config.h"

static struct Client *clients = NULL, *focus = NULL, *mouse_dc = NULL;
static struct Monitor *monitors = NULL;
static struct Monitor *saved_monitors[SAVE_SLOTS] = { 0 };
static int saved_monitor_nums[SAVE_SLOTS] = { 0 };
static int mouse_dx, mouse_dy, mouse_ocx, mouse_ocy, mouse_ocw, mouse_och;
static int winsize_min_w, winsize_min_h;
static Atom atom_net[AtomNetLAST], atom_wm[AtomWMLAST], atom_motif, atom_state,
            atom_ipc;
static Cursor cursor_normal;
static Display *dpy;
static Pixmap dec_tiles[DecStateLAST][DecLAST];
static Window nofocus, root;
static XftColor font_color[DecStateLAST];
static XftFont *font[FontLAST];
static int monitors_num = 0, selmon = 0, prevmon = 0;
static bool running = true;
static bool restart = false;
static int screen;
static int (*xerrorxlib)(Display *, XErrorEvent *);

static void cleanup(void);
static struct Client *client_get_for_decoration(
        Window win,
        enum DecorationWindowLocation *which
);
static struct Client *client_get_for_window(Window win);
static void client_save(struct Client *c);
static void client_update_title(struct Client *c);
static void decorations_create(struct Client *c);
static void decorations_destroy(struct Client *c);
static void decorations_draw(struct Client *c,
                             enum DecorationWindowLocation which);
static char *decorations_ff_to_x(enum DecState s, uint32_t *width,
                                 uint32_t *height);
static Pixmap decorations_get_pm(GC gc, XImage **ximg, enum DecorationLocation l,
                                 enum DecState s);
static void decorations_load(void);
static void decorations_map(struct Client *c);
static XImage *decorations_to_ximg(char *data, uint32_t width, uint32_t height);
static void draw_text(Drawable d, XftFont *xfont, XftColor *col, int x, int y,
                      int w, bool centered, char *s);
static void handle_clientmessage(XEvent *e);
static void handle_configurenotify(XEvent *e);
static void handle_configurerequest(XEvent *e);
static void handle_expose(XEvent *e);
static void handle_focusin(XEvent *e);
static void handle_maprequest(XEvent *e);
static void handle_propertynotify(XEvent *e);
static void handle_unmapnotify(XEvent *e);
static void ipc_client_center_floating(char arg);
static void ipc_client_close(char arg);
static void ipc_client_floating_toggle(char arg);
static void ipc_client_fullscreen_toggle(char arg);
static void ipc_client_kill(char arg);
static void ipc_client_maximize_floating(char arg);
static void ipc_client_move_list(char arg);
static void ipc_client_move_mouse(char arg);
static void ipc_client_resize_mouse(char arg);
static void ipc_client_select_adjacent(char arg);
static void ipc_client_select_recent(char arg);
static void ipc_client_switch_monitor_adjacent(char arg);
static void ipc_client_switch_workspace(char arg);
static void ipc_client_switch_workspace_adjacent(char arg);
static void ipc_floaters_collect(char arg);
static void ipc_layout_set(char arg);
static void ipc_monitor_select_adjacent(char arg);
static void ipc_monitor_select_recent(char arg);
static void ipc_placement_store(char arg);
static void ipc_placement_use(char arg);
static void ipc_urgency_clear_visible(char arg);
static void ipc_wm_quit(char arg);
static void ipc_wm_restart(char arg);
static void ipc_workspace_select(char arg);
static void ipc_workspace_select_adjacent(char arg);
static void ipc_workspace_select_recent(char arg);
static void layout_float(int m);
static void layout_monocle(int m);
static void layout_tile(int m);
static void manage(Window win, XWindowAttributes *wa);
static void manage_apply_gaps(struct Client *c);
static void manage_apply_rules(struct Client *c);
static void manage_apply_size(struct Client *c);
static void manage_arrange(int m);
static void manage_clear_urgency(struct Client *c);
static void manage_client_gone(struct Client *c, bool rearrange);
static void manage_ewmh_evaluate_hints(struct Client *c);
static void manage_fit_on_monitor(struct Client *c);
static void manage_focus_add_head(struct Client *c);
static void manage_focus_add_tail(struct Client *c);
static void manage_focus_remove(struct Client *c);
static void manage_focus_set(struct Client *c);
static void manage_fullscreen(struct Client *c);
static void manage_goto_monitor(int i, bool force);
static void manage_goto_workspace(int i, bool force);
static void manage_hide(struct Client *c);
static void manage_icccm_apply_size_hints(struct Client *c);
static void manage_icccm_evaluate_hints(struct Client *c);
static void manage_motif_evaluate_hints(struct Client *c, bool rearrange);
static void manage_raisefocus(struct Client *c);
static void manage_raisefocus_first_matching(void);
static void manage_set_decorations(struct Client *c, bool decorated);
static void manage_show(struct Client *c);
static void manage_unfullscreen(struct Client *c);
static void manage_xfocus(struct Client *c);
static void manage_xraise(struct Client *c);
static int manage_xsend_icccm(struct Client *c, Atom atom);
static void publish_state(void);
static void run(void);
static void scan(void);
static void setup(void);
static void setup_hints(void);
static int setup_monitors_compare(const void *a, const void *b);
static bool setup_monitors_is_duplicate(XRRCrtcInfo *ci, bool *chosen,
                                        XRRScreenResources *sr);
static void setup_monitors_read(void);
static int setup_monitors_wsdef(int mi, int monitors_num);
static void shutdown_monitors_free(void);
static int xerror(Display *dpy, XErrorEvent *ee);

static void (*ipc_handler[IPCLast])(char arg) = {
    [IPCClientCenterFloating] = ipc_client_center_floating,
    [IPCClientClose] = ipc_client_close,
    [IPCClientFloatingToggle] = ipc_client_floating_toggle,
    [IPCClientFullscreenToggle] = ipc_client_fullscreen_toggle,
    [IPCClientKill] = ipc_client_kill,
    [IPCClientMaximizeFloating] = ipc_client_maximize_floating,
    [IPCClientMoveList] = ipc_client_move_list,
    [IPCClientMoveMouse] = ipc_client_move_mouse,
    [IPCClientResizeMouse] = ipc_client_resize_mouse,
    [IPCClientSelectAdjacent] = ipc_client_select_adjacent,
    [IPCClientSelectRecent] = ipc_client_select_recent,
    [IPCClientSwitchMonitorAdjacent] = ipc_client_switch_monitor_adjacent,
    [IPCClientSwitchWorkspace] = ipc_client_switch_workspace,
    [IPCClientSwitchWorkspaceAdjacent] = ipc_client_switch_workspace_adjacent,
    [IPCFloatersCollect] = ipc_floaters_collect,
    [IPCLayoutSet] = ipc_layout_set,
    [IPCMonitorSelectAdjacent] = ipc_monitor_select_adjacent,
    [IPCMonitorSelectRecent] = ipc_monitor_select_recent,
    [IPCPlacementStore] = ipc_placement_store,
    [IPCPlacementUse] = ipc_placement_use,
    [IPCUrgencyClearVisible] = ipc_urgency_clear_visible,
    [IPCWMQuit] = ipc_wm_quit,
    [IPCWMRestart] = ipc_wm_restart,
    [IPCWorkspaceSelect] = ipc_workspace_select,
    [IPCWorkspaceSelectAdjacent] = ipc_workspace_select_adjacent,
    [IPCWorkspaceSelectRecent] = ipc_workspace_select_recent,
};

static void (*x11_handler[LASTEvent])(XEvent *e) = {
    [ClientMessage] = handle_clientmessage,
    [ConfigureNotify] = handle_configurenotify,
    [ConfigureRequest] = handle_configurerequest,
    [Expose] = handle_expose,
    [FocusIn] = handle_focusin,
    [MapRequest] = handle_maprequest,
    [PropertyNotify] = handle_propertynotify,
    [UnmapNotify] = handle_unmapnotify,
};

static void (*layouts[LALast])(int m) = {
    [LAFloat] = layout_float,
    [LAMonocle] = layout_monocle,
    [LATile] = layout_tile,
};


void
cleanup(void)
{
    size_t i, j;

    while (clients != NULL)
        manage_client_gone(clients, false);

    for (i = DecStateNormal; i <= DecStateUrgent; i++)
        for (j = DecTopLeft; j <= DecBottomRight; j++)
            XFreePixmap(dpy, dec_tiles[i][j]);

    XDeleteProperty(dpy, root, atom_state);
    XDeleteProperty(dpy, root, atom_net[AtomNetSupported]);
    XFreeCursor(dpy, cursor_normal);

    XCloseDisplay(dpy);
}

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
        D fprintf(stderr, __NAME_WM__": Title of client %p could not be read "
                  "from EWMH\n", (void *)c);
        if (!XGetTextProperty(dpy, c->win, &tp, XA_WM_NAME))
        {
            D fprintf(stderr, __NAME_WM__": Title of client %p could not be "
                      "read from ICCCM\n", (void *)c);
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
        D fprintf(stderr, __NAME_WM__": Title of client %p read as verbatim "
                  "string\n", (void *)c);
    }
    else
    {
        /* This is a particularly gnarly function. Props to dwm which has
         * figured out how to use it. */
        if (XmbTextPropertyToTextList(dpy, &tp, &slist, &count) >= Success &&
            count > 0 && *slist)
        {
            strncpy(c->title, slist[0], sizeof c->title - 1);
            XFreeStringList(slist);
            D fprintf(stderr, __NAME_WM__": Title of client %p read as "
                      "XmbText\n", (void *)c);
        }
    }

    c->title[sizeof c->title - 1] = 0;

    XFree(tp.value);

    D fprintf(stderr, __NAME_WM__": Title of client %p is now '%s'\n", (void *)c,
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
    XClassHint ch = {
        .res_class = __NAME_WM_CAPITALIZED__,
        .res_name = "decoration",
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

        /* Setting hints on decoration windows gives tools a change to
         * handle them differently. EWMH does not account for decoration
         * windows because reparenting usually happens. */
        XSetClassHint(dpy, c->decwin[i], &ch);
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
decorations_draw(struct Client *c, enum DecorationWindowLocation which)
{
    int x, y, w, h;
    enum DecState state = DecStateNormal;
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
        state = DecStateUrgent;
    else if (c == focus && c->mon == selmon && VIS_ON_SELMON(c))
        state = DecStateSelect;

    w = dgeo.left_width + c->w + dgeo.right_width;
    h = dgeo.top_height + c->h + dgeo.bottom_height;

    gc = XCreateGC(dpy, root, 0, NULL);
    gc_tiled = XCreateGC(dpy, root, 0, NULL);
    dec_pm = XCreatePixmap(dpy, root, w, h, DefaultDepth(dpy, screen));

    XSetFillStyle(dpy, gc_tiled, FillTiled);

    XSetTile(dpy, gc_tiled, dec_tiles[state][DecTop]);
    XSetTSOrigin(dpy, gc_tiled, 0, 0);
    XFillRectangle(dpy, dec_pm, gc_tiled, 0, 0, w, dec_coords[DecTop].h);

    XSetTile(dpy, gc_tiled, dec_tiles[state][DecBottom]);
    XSetTSOrigin(dpy, gc_tiled, 0, h - dgeo.bottom_height);
    XFillRectangle(dpy, dec_pm, gc_tiled, 0, h - dgeo.bottom_height,
                   w, dec_coords[DecBottom].h);

    XSetTile(dpy, gc_tiled, dec_tiles[state][DecLeft]);
    XSetTSOrigin(dpy, gc_tiled, 0, dgeo.top_height);
    XFillRectangle(dpy, dec_pm, gc_tiled, 0, dgeo.top_height,
                   dec_coords[DecLeft].w, c->h);

    XSetTile(dpy, gc_tiled, dec_tiles[state][DecRight]);
    XSetTSOrigin(dpy, gc_tiled, w - dgeo.right_width, dgeo.top_height);
    XFillRectangle(dpy, dec_pm, gc_tiled, w - dgeo.right_width, dgeo.top_height,
                   dec_coords[DecRight].w, c->h);

    XCopyArea(dpy, dec_tiles[state][DecTopLeft], dec_pm, gc,
              0, 0,
              dec_coords[DecTopLeft].w, dec_coords[DecTopLeft].h,
              0, 0);

    XCopyArea(dpy, dec_tiles[state][DecTopRight], dec_pm, gc,
              0, 0,
              dec_coords[DecTopRight].w, dec_coords[DecTopRight].h,
              w - dec_coords[DecTopRight].w, 0);

    XCopyArea(dpy, dec_tiles[state][DecBottomLeft], dec_pm, gc,
              0, 0,
              dec_coords[DecBottomLeft].w, dec_coords[DecBottomLeft].h,
              0, h - dec_coords[DecBottomLeft].h);

    XCopyArea(dpy, dec_tiles[state][DecBottomRight], dec_pm, gc,
              0, 0,
              dec_coords[DecBottomRight].w, dec_coords[DecBottomRight].h,
              w - dec_coords[DecBottomRight].w, h - dec_coords[DecBottomLeft].h);

    if (dec_has_title)
    {
        x = dec_title.left_offset;
        y = dec_title.baseline_top_offset;
        w = (dgeo.left_width + c->w + dgeo.right_width) - dec_title.left_offset
            - dec_title.right_offset;
        draw_text(dec_pm, font[FontTitle], &font_color[state], x, y, w,
                  center_title, c->title);
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

char *
decorations_ff_to_x(enum DecState s, uint32_t *width, uint32_t *height)
{
    uint32_t x, y;
    uint8_t *ff_img = NULL;
    uint16_t *ff_img_data = NULL;
    uint32_t *ximg_data = NULL;

    /* Read size from farbfeld header and convert the farbfeld image to
     * something XCreateImage can understand. */

    switch (s)
    {
        case DecStateNormal:  ff_img = dec_img_normal;  break;
        case DecStateSelect:  ff_img = dec_img_select;  break;
        case DecStateUrgent:  ff_img = dec_img_urgent;  break;
        default:
            D fprintf(stderr, __NAME_WM__": State %d in decorations_ff_to_x() "
                      "is not a known value\n", s);
    }

    /* ff_img is an array of bytes, so we do some little pointer
     * arithmetic to get to the actual data and the header. */
    ff_img_data = (uint16_t *)&ff_img[16];

    *width = ntohl(*((uint32_t *)&ff_img[8]));
    *height = ntohl(*((uint32_t *)&ff_img[12]));

    ximg_data = calloc(*width * *height, sizeof (uint32_t));
    assert(ximg_data != NULL);

    for (y = 0; y < *height; y++)
    {
        for (x = 0; x < *width; x++)
        {
            ximg_data[y * *width + x] =
                ((ntohs(ff_img_data[(y * *width + x) * 4    ]) / 256) << 16) |
                ((ntohs(ff_img_data[(y * *width + x) * 4 + 1]) / 256) << 8) |
                 (ntohs(ff_img_data[(y * *width + x) * 4 + 2]) / 256);
        }
    }

    return (char *)ximg_data;
}

Pixmap
decorations_get_pm(GC gc, XImage **ximg, enum DecorationLocation l,
                   enum DecState s)
{
    Pixmap p;

    p = XCreatePixmap(dpy, root, dec_coords[l].w, dec_coords[l].h,
                      DefaultDepth(dpy, screen));
    XPutImage(dpy, p, gc, ximg[s],
              dec_coords[l].x, dec_coords[l].y,
              0, 0,
              dec_coords[l].w, dec_coords[l].h);

    return p;
}

void
decorations_load(void)
{
    char *convertible_to_ximg[DecStateLAST];
    XImage *ximg[DecStateLAST];
    size_t i, j;
    uint32_t width, height;
    GC gc;
    int left_corner_w, right_corner_w;
    int top_corner_h, bottom_corner_h;

    /* The decorations are embedded in the final binary and can be
     * accessed using variable names like "dec_img_normal". They are
     * arrays of bytes. File format is farbfeld:
     *
     * http://tools.suckless.org/farbfeld/
     *
     * We now first convert each farbfeld image into an XImage. XImages
     * live in client memory, so we can pass xlib a pointer to our data.
     *
     * XImages will then be copied to Pixmaps, which live on the server.
     * Note that we do some slicing and dicing first, splitting each
     * XImage into the individual tiles ("top left", "top right", and so
     * on). Each tile will become a Pixmap. Later on, we will only use
     * those Pixmaps to actually draw decorations. */

    for (i = DecStateNormal; i <= DecStateUrgent; i++)
    {
        convertible_to_ximg[i] = decorations_ff_to_x(i, &width, &height);
        ximg[i] = decorations_to_ximg(convertible_to_ximg[i], width, height);
    }

    gc = XCreateGC(dpy, root, 0, NULL);
    for (i = DecStateNormal; i <= DecStateUrgent; i++)
        for (j = DecTopLeft; j <= DecBottomRight; j++)
            dec_tiles[i][j] = decorations_get_pm(gc, ximg, j, i);
    XFreeGC(dpy, gc);

    for (i = DecStateNormal; i <= DecStateUrgent; i++)
        /* Note: This also frees convertible_to_ximg[i] */
        XDestroyImage(ximg[i]);

    /* Find the minimum window size that allows for the decorations to
     * be fully drawn. Smaller window size might result in visual
     * artifacts. Windows must be at least 1x1 pixels big, though. */
    left_corner_w = dec_coords[DecTopLeft].w > dec_coords[DecBottomLeft].w ?
                    dec_coords[DecTopLeft].w : dec_coords[DecBottomLeft].w;
    right_corner_w = dec_coords[DecTopRight].w > dec_coords[DecBottomRight].w ?
                     dec_coords[DecTopRight].w : dec_coords[DecBottomRight].w;

    top_corner_h = dec_coords[DecTopLeft].h > dec_coords[DecTopRight].h ?
                   dec_coords[DecTopLeft].h : dec_coords[DecTopRight].h;
    bottom_corner_h = dec_coords[DecBottomLeft].h > dec_coords[DecBottomRight].h ?
                      dec_coords[DecBottomLeft].h : dec_coords[DecBottomRight].h;

    winsize_min_w = (left_corner_w - dgeo.left_width) +
                    (right_corner_w - dgeo.right_width);
    winsize_min_h = (top_corner_h - dgeo.top_height) +
                    (bottom_corner_h - dgeo.bottom_height);

    winsize_min_w = winsize_min_w < 1 ? 1 : winsize_min_w;
    winsize_min_h = winsize_min_h < 1 ? 1 : winsize_min_h;
}

void
decorations_map(struct Client *c)
{
    size_t i;

    for (i = DecWinTop; i <= DecWinBottom; i++)
        XMapRaised(dpy, c->decwin[i]);
}

XImage *
decorations_to_ximg(char *data, uint32_t width, uint32_t height)
{
    return XCreateImage(dpy, DefaultVisual(dpy, screen), 24, ZPixmap, 0,
                        data, width, height, 32, 0);
}

void
draw_text(Drawable d, XftFont *xfont, XftColor *col, int x, int y, int w,
          bool centered, char *s)
{
    XftDraw *xd;
    XGlyphInfo ext;
    int len;

    /* Just a quick sanity check */
    if (strlen(s) > 4096)
        return;

    /* Reduce length until the rendered text can fit into the desired
     * width -- this is "kind of"-ish UTF-8 compatible. It creates a
     * better visual result than just clipping the region. */
    for (len = strlen(s); len >= 0; len--)
    {
        XftTextExtentsUtf8(dpy, xfont, (XftChar8 *)s, len, &ext);
        if (ext.xOff < w)
            break;
    }

    if (len == 0)
        return;

    if (centered)
        x = x + 0.5 * (w - ext.xOff);

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
        arg = cme->data.b[1];

        /* Note: Checking for "cmd >= 0" creates a compiler warning on
         * LLVM but not on GCC. The warning says that enums are always
         * unsigned. I want to be on the safe side here, though. */
        if (cmd >= 0 && cmd < IPCLast && ipc_handler[cmd])
            ipc_handler[cmd](arg);
    }
    else if (cme->message_type == atom_net[AtomNetWMState])
    {
        if ((c = client_get_for_window(cme->window)) == NULL)
        {
            D fprintf(stderr, __NAME_WM__": Window %lu sent EWMH message, but "
                      "we don't manage this window\n", cme->window);
        }

        if (c)
        {
            /* An EWMH client state message might contain two properties
             * to alter, data.l[1] and data.l[2] */
            if ((Atom)cme->data.l[1] == atom_net[AtomNetWMStateFullscreen] ||
                (Atom)cme->data.l[2] == atom_net[AtomNetWMStateFullscreen])
            {
                D fprintf(stderr, __NAME_WM__": Client %p requested EWMH "
                          "fullscreen\n", (void *)c);

                /* 0 = remove, 1 = add, 2 = toggle */
                if (c->fullscreen && (cme->data.l[0] == 0 ||
                                      cme->data.l[0] == 2))
                {
                    manage_unfullscreen(c);
                }
                else if (!c->fullscreen && (cme->data.l[0] == 1 ||
                                            cme->data.l[0] == 2))
                {
                    manage_fullscreen(c);
                }
            }
            else
            {
                an = XGetAtomName(dpy, cme->message_type);
                D fprintf(stderr, __NAME_WM__": Received EWMH message with "
                          "unknown action: %lu, %s\n", cme->data.l[0], an);
                if (an)
                    XFree(an);
            }
        }
    }
    else
    {
        an = XGetAtomName(dpy, cme->message_type);
        D fprintf(stderr, __NAME_WM__": Received client message with "
                  "unknown type: %lu, %s\n", cme->message_type, an);
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

    D fprintf(stderr, __NAME_WM__": ConfigureNotify received, reconfiguring "
              "monitors \n");

    shutdown_monitors_free();
    setup_monitors_read();

    for (c = clients; c; c = c->next)
        c->mon = selmon;

    /* Hide everything, then unhide what should be visible on the
     * default workspace */
    for (c = clients; c; c = c->next)
        manage_hide(c);

    for (c = clients; c; c = c->next)
        if (VIS_ON_SELMON(c))
            manage_show(c);

    manage_raisefocus_first_matching();
    manage_arrange(selmon);

    XWarpPointer(dpy, None, root, 0, 0, 0, 0,
                 monitors[selmon].wx + monitors[selmon].ww / 2,
                 monitors[selmon].wy + monitors[selmon].wh / 2);
}

void
handle_configurerequest(XEvent *e)
{
    XConfigureRequestEvent *ev = &e->xconfigurerequest;
    XConfigureEvent ce;
    XWindowChanges wc;
    struct Client *c = NULL;
    bool dirty = false;

    if ((c = client_get_for_window(ev->window)))
    {
        /* This is a known client. However, only floating clients are
         * allowed to resize themselves. If they want to do that,
         * manage_apply_size() will take care of actually changing their
         * size.
         *
         * For all clients, we call XSendEvent(). This creates a
         * synthetic event, so the server is not being told to change
         * anything. It merely informs the client about its current
         * configuration. */
        if (SOMEHOW_FLOATING(c))
        {
            if (ev->value_mask & CWWidth)
            {
                c->w = ev->width;
                dirty = true;
            }
            if (ev->value_mask & CWHeight)
            {
                c->h = ev->height;
                dirty = true;
            }

            if (dirty)
            {
                D fprintf(stderr, __NAME_WM__": Client %p resized itself to "
                          "%d %d\n", (void *)c, c->w, c->h);
                manage_apply_size(c);
            }
        }

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
         * just what he wants.
         *
         * This calls XConfigureWindow() because we want the server to
         * actually execute what the client requested. */
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
handle_expose(XEvent *e)
{
    XExposeEvent *ev = &e->xexpose;
    struct Client *c;
    enum DecorationWindowLocation which;

    if ((c = client_get_for_decoration(ev->window, &which)) == NULL)
        return;

    decorations_draw(c, which);
}

void
handle_focusin(XEvent *e)
{
    XFocusChangeEvent *ev = &e->xfocus;

    /* Some clients try to grab focus without being the selected window.
     * We don't want that. Unfortunately, there is no way to *prevent*
     * this. The only thing we can do, is to revert the focus to what we
     * think should have it. */

    if (focus && ev->window != focus->win)
        manage_xfocus(focus);
}

void
handle_maprequest(XEvent *e)
{
    XMapRequestEvent *ev = &e->xmaprequest;
    XWindowAttributes wa;

    /* This is where we start managing a new window (unless it has been
     * detected by scan() at startup). Note that windows might have
     * existed before a MapRequest. Explicitly ignore windows with
     * override_redirect being True to allow popups, bars, panels, ... */

    if (!XGetWindowAttributes(dpy, ev->window, &wa))
        return;
    if (wa.override_redirect)
        return;

    manage(ev->window, &wa);
}

void
handle_propertynotify(XEvent *e)
{
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
            decorations_draw(c, DecWinLAST);
        }
        else if (ev->atom == XA_WM_HINTS)
        {
            D fprintf(stderr, __NAME_WM__": Client %p has changed its WM_HINTS, "
                      "updating\n", (void *)c);
            manage_icccm_evaluate_hints(c);
        }
        else if (ev->atom == XA_WM_NORMAL_HINTS)
        {
            D fprintf(stderr, __NAME_WM__": Client %p has changed its "
                      "WM_NORMAL_HINTS, updating\n", (void *)c);
            manage_icccm_evaluate_hints(c);
        }
        else if (ev->atom == atom_motif)
        {
            D fprintf(stderr, __NAME_WM__": Client %p has changed its "
                      "_MOTIF_WM_HINTS, updating\n", (void *)c);
            manage_motif_evaluate_hints(c, true);
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
            D fprintf(stderr, __NAME_WM__": PropertyNotify about unhandled "
                      "atom '%s'\n", an);
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

    /* This is the counterpart to a MapRequest. It's where we stop
     * managing a window. The window might not necessarily be destroyed,
     * it could be reused later on -- but that doesn't matter to us
     * because it's invisible until another MapRequest occurs.
     *
     * ICCCM 4.1.4 says that you either get a synthetic or unsynthetic
     * UnmapNotify event. In either case, the WM should issue the
     * transition from NormalState to WithdrawnState by unmapping the
     * window and removing the WM_STATE property. */
    if ((c = client_get_for_window(ev->window)))
    {
        XUnmapWindow(dpy, c->win);
        manage_client_gone(c, true);
    }
}

void
ipc_client_center_floating(char arg)
{
    (void)arg;

    if (!SOMETHING_FOCUSED)
        return;

    if (!SOMEHOW_FLOATING(focus))
        return;

    focus->x = monitors[focus->mon].wx + 0.5 * (monitors[focus->mon].ww - focus->w
                                                - focus->m_left - focus->m_right);
    focus->y = monitors[focus->mon].wy + 0.5 * (monitors[focus->mon].wh - focus->h
                                                - focus->m_top - focus->m_bottom);

    focus->x += focus->m_left;
    focus->y += focus->m_top;

    manage_apply_size(focus);
}

void
ipc_client_close(char arg)
{
    (void)arg;

    if (!SOMETHING_FOCUSED)
        return;

    /* This call asks the client to please close itself gracefully */
    manage_xsend_icccm(focus, atom_wm[AtomWMDeleteWindow]);
}

void
ipc_client_floating_toggle(char arg)
{
    (void)arg;

    if (!SOMETHING_FOCUSED)
        return;

    focus->floating = !focus->floating;
    manage_arrange(selmon);
}

void
ipc_client_fullscreen_toggle(char arg)
{
    (void)arg;

    if (!SOMETHING_FOCUSED)
        return;

    if (focus->fullscreen)
        manage_unfullscreen(focus);
    else
        manage_fullscreen(focus);
}

void
ipc_client_kill(char arg)
{
    (void)arg;

    if (!SOMETHING_FOCUSED)
        return;

    /* This brutally kills the X11 connection of the client. Use only as
     * a last resort. */
    XKillClient(dpy, focus->win);
}

void
ipc_client_maximize_floating(char arg)
{
    (void)arg;

    if (!SOMETHING_FOCUSED)
        return;

    if (!SOMEHOW_FLOATING(focus))
        return;

    focus->x = monitors[focus->mon].wx;
    focus->y = monitors[focus->mon].wy;
    focus->w = monitors[focus->mon].ww;
    focus->h = monitors[focus->mon].wh;

    focus->x += focus->m_left;
    focus->y += focus->m_top;
    focus->w -= focus->m_left + focus->m_right;
    focus->h -= focus->m_top + focus->m_bottom;

    manage_apply_gaps(focus);
    manage_apply_size(focus);
}

void
ipc_client_move_list(char arg)
{
    bool use_next = false;
    struct Client *c, *one = NULL, *two = NULL,
                  *before_one = NULL, *before_two = NULL;

    if (!SOMETHING_FOCUSED)
        return;

    if (focus->fullscreen)
        return;

    if (arg < 0)
    {
        /* Find visible client before "focus" */
        for (c = clients; c; c = c->next)
        {
            if (VIS_ON_SELMON(c))
            {
                if (c == focus)
                    break;
                else
                {
                    /* List order: "one" precedes "two" */
                    one = c;
                    two = focus;
                }
            }
        }
    }
    else
    {
        /* Find visible client after "focus" */
        for (c = focus; c; c = c->next)
        {
            if (VIS_ON_SELMON(c))
            {
                if (c == focus)
                    use_next = true;
                else if (use_next)
                {
                    one = focus;
                    two = c;
                    break;
                }
            }
        }
    }

    D fprintf(stderr, __NAME_WM__": Found partners: %p, %p\n",
              (void *)one, (void *)two);

    if (one == NULL || two == NULL)
        return;

    /* The following method does not really "swap" the two list
     * elements. It simply removes the second item from the list and
     * then prepends it in front of "one". Thus, "two" now comes before
     * "one", which is all we wanted. */

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
        D fprintf(stderr, __NAME_WM__": Mouse move: down at %d, %d over %lu\n",
                  x, y, child);

        mouse_dc = NULL;

        if ((c = client_get_for_window(child)) ||
            (c = client_get_for_decoration(child, NULL)))
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
        D fprintf(stderr, __NAME_WM__": Mouse move: motion to %d, %d\n", x, y);

        if (mouse_dc)
        {
            dx = x - mouse_dx;
            dy = y - mouse_dy;

            mouse_dc->x = mouse_ocx + dx;
            mouse_dc->y = mouse_ocy + dy;

            manage_apply_size(mouse_dc);
        }
    }
    else if (arg == 2)
        mouse_dc = NULL;
}

void
ipc_client_resize_mouse(char arg)
{
    int x, y, di, dx, dy;
    unsigned int dui;
    Window child, dummy;
    struct Client *c;

    XQueryPointer(dpy, root, &dummy, &child, &x, &y, &di, &di, &dui);

    if (arg == 0)
    {
        D fprintf(stderr, __NAME_WM__": Mouse resize: down at %d, %d over %lu\n",
                  x, y, child);

        mouse_dc = NULL;

        if ((c = client_get_for_window(child)) ||
            (c = client_get_for_decoration(child, NULL)))
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
        D fprintf(stderr, __NAME_WM__": Mouse resize: motion to %d, %d\n", x, y);

        if (mouse_dc)
        {
            dx = x - mouse_dx;
            dy = y - mouse_dy;

            mouse_dc->w = mouse_ocw + dx;
            mouse_dc->h = mouse_och + dy;

            manage_apply_size(mouse_dc);
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
        /* Start at "focus" and search the next visible client. If that
         * doesn't work, then start from the beginning of the list. */

        for (c = focus->next; c && !VIS_ON_SELMON(c); c = c->next)
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
         * "focus", we will not enter the loop body, thus to_select will
         * still point to the visible client before c. */
        for (c = clients; c != focus; c = c->next)
            if (VIS_ON_SELMON(c))
                to_select = c;

        /* Nothing found? Then start at "focus" and look at all clients
         * after c. After this loop, to_select will point to the last
         * visible client in the list. */
        if (!to_select)
            for (c = focus->next; c; c = c->next)
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
     * client is "focus" itself, so we can easily skip it. */

    for (c = focus->focus_next; c; c = c->focus_next)
        if (VIS_ON_SELMON(c))
            break;

    if (c)
        manage_raisefocus(c);
}

void
ipc_client_switch_monitor_adjacent(char arg)
{
    int i, old_mon;

    /* Move currently selected client to an adjacent monitor, causing
     * both monitors to be re-arranged */

    if (!SOMETHING_FOCUSED)
        return;

    if (focus->fullscreen)
        return;

    i = selmon;
    i += arg;
    i = i < 0 ? 0 : i;
    i = i >= monitors_num ? monitors_num - 1 : i;

    old_mon = focus->mon;
    focus->mon = i;
    focus->workspace = monitors[i].active_workspace;

    /* If the client is floating or the target layout is floating, then
     * we need to re-fit the client and apply the newly calculated size.
     * This has no effect for non-floaters because we call
     * manage_arrange() afterwards. */
    manage_fit_on_monitor(focus);

    manage_raisefocus_first_matching();
    manage_arrange(old_mon);
    manage_arrange(i);
}

void
ipc_client_switch_workspace(char arg)
{
    int i;

    if (!SOMETHING_FOCUSED)
        return;

    if (focus->fullscreen)
        return;

    i = arg;
    i = i < WORKSPACE_MIN ? WORKSPACE_MIN : i;
    i = i > WORKSPACE_MAX ? WORKSPACE_MAX : i;

    focus->workspace = i;

    /* Note: This call is not meant to switch the workspace but to force
     * rearrangement of the current workspace */
    manage_goto_workspace(monitors[selmon].active_workspace, true);
}

void
ipc_client_switch_workspace_adjacent(char arg)
{
    int i;

    if (!SOMETHING_FOCUSED)
        return;

    if (focus->fullscreen)
        return;

    i = monitors[selmon].active_workspace;
    i += arg;
    i = i < WORKSPACE_MIN ? WORKSPACE_MIN : i;
    i = i > WORKSPACE_MAX ? WORKSPACE_MAX : i;

    focus->workspace = i;

    /* Note: This call is not meant to switch the workspace but to force
     * rearrangement of the current workspace */
    manage_goto_workspace(monitors[selmon].active_workspace, true);
}

void
ipc_floaters_collect(char arg)
{
    struct Client *c;

    (void)arg;

    for (c = clients; c; c = c->next)
        if (VIS_ON_SELMON(c) && SOMEHOW_FLOATING(c))
            manage_fit_on_monitor(c);
}

void
ipc_layout_set(char arg)
{
    int i;

    i = arg;

    if (i < 0 || i >= LALast || layouts[i] == NULL)
    {
        fprintf(stderr, __NAME_WM__": Invalid layout requested: %d\n", i);
        return;
    }

    monitors[selmon].layouts[monitors[selmon].active_workspace] = i;
    manage_arrange(selmon);
}

void
ipc_monitor_select_adjacent(char arg)
{
    int i;

    i = selmon;
    i += arg;
    i %= monitors_num;
    i = i < 0 ? monitors_num + i : i;

    manage_goto_monitor(i, false);
}

void
ipc_monitor_select_recent(char arg)
{
    (void)arg;

    manage_goto_monitor(prevmon, false);
}

void
ipc_urgency_clear_visible(char arg)
{
    struct Client *c;
    int m;
    bool visible;

    (void)arg;

    /* Reset urgency hint on all currently visible clients */

    for (c = clients; c; c = c->next)
    {
        visible = false;

        for (m = 0; !visible && m < monitors_num; m++)
            if (VIS_ON_M(c, m))
                visible = true;

        if (visible)
        {
            manage_clear_urgency(c);
            decorations_draw(c, DecWinLAST);
        }
    }

    publish_state();
}

void
ipc_placement_store(char arg)
{
    struct Client *c;
    size_t ai;
    int m;

    /* Things to save:
     *
     * - Placement (workspace and monitor) of each client
     * - Active workspace on each monitor
     * - Active layouts on each monitor
     *
     * We store monitor info by copying the whole "monitors" array. */

    if (arg < 0 || arg >= SAVE_SLOTS)
    {
        fprintf(stderr, __NAME_WM__": Slot %d is invalid\n", arg);
        return;
    }

    ai = arg;

    for (c = clients; c; c = c->next)
    {
        c->saved_monitors[ai] = c->mon;
        c->saved_workspaces[ai] = c->workspace;
    }

    if (saved_monitors[ai])
        free(saved_monitors[ai]);

    saved_monitors[ai] = ecalloc(monitors_num, sizeof (struct Monitor));
    saved_monitor_nums[ai] = monitors_num;
    memcpy(saved_monitors[ai], monitors, monitors_num * sizeof (struct Monitor));

    D
    {
        /* Printing layout of workspace 1 just to get a rough idea */
        fprintf(stderr, __NAME_WM__": Saved monitors in slot %lu:\n", ai);
        for (m = 0; m < monitors_num; m++)
            fprintf(stderr, __NAME_WM__":  %d: %d %d, %d\n", m,
                    saved_monitors[ai][m].mx,
                    saved_monitors[ai][m].my,
                    saved_monitors[ai][m].layouts[1]);
    }

    publish_state();
}

void
ipc_placement_use(char arg)
{
    struct Client *c;
    int m;
    size_t ai;

    /* The opposite of ipc_placement_store(): Restore clients'
     * workspaces and monitors (if applicable), plus active workspaces
     * and layouts on each monitor (if applicable) */

    if (arg < 0 || arg >= SAVE_SLOTS)
    {
        fprintf(stderr, __NAME_WM__": Slot %d is invalid\n", arg);
        return;
    }

    ai = arg;

    if (saved_monitors[ai] == NULL)
        return;

    for (c = clients; c; c = c->next)
    {
        if (c->saved_monitors[ai] >= 0 && c->saved_monitors[ai] < monitors_num)
            c->mon = c->saved_monitors[ai];

        if (c->saved_workspaces[ai] >= WORKSPACE_MIN &&
            c->saved_workspaces[ai] <= WORKSPACE_MAX)
            c->workspace = c->saved_workspaces[ai];
    }

    for (m = 0; m < monitors_num && m < saved_monitor_nums[ai]; m++)
    {
        monitors[m].active_workspace = saved_monitors[ai][m].active_workspace;
        memcpy(monitors[m].layouts, saved_monitors[ai][m].layouts,
               sizeof monitors[m].layouts);
    }

    /* Re-arrange last-to-first, so, when we finish, the focused monitor
     * will be index 0 */
    for (m = monitors_num - 1; m >= 0; m--)
    {
        manage_goto_monitor(m, true);
        manage_goto_workspace(monitors[m].active_workspace, true);
    }

    /* Restore these last because they have been altered by the loop
     * above */
    for (m = 0; m < monitors_num && m < saved_monitor_nums[ai]; m++)
        monitors[m].recent_workspace = saved_monitors[ai][m].recent_workspace;
}

void
ipc_wm_quit(char arg)
{
    (void)arg;

    running = false;

    D fprintf(stderr, __NAME_WM__": Quitting\n");
}

void
ipc_wm_restart(char arg)
{
    (void)arg;

    restart = true;
    running = false;

    D fprintf(stderr, __NAME_WM__": Quitting for restart\n");
}

void
ipc_workspace_select(char arg)
{
    int i;

    i = arg;
    manage_goto_workspace(i, false);
}

void
ipc_workspace_select_adjacent(char arg)
{
    int i;

    i = monitors[selmon].active_workspace;
    i += arg;
    manage_goto_workspace(i, false);
}

void
ipc_workspace_select_recent(char arg)
{
    (void)arg;

    manage_goto_workspace(monitors[selmon].recent_workspace, false);
}

void
layout_float(int m)
{
    (void)m;
}

void
layout_monocle(int m)
{
    struct Client *c;

    for (c = clients; c; c = c->next)
    {
        if (VIS_ON_M(c, m) && !c->floating && !c->fullscreen)
        {
            c->x = monitors[c->mon].wx + c->m_left;
            c->y = monitors[c->mon].wy + c->m_top;
            c->w = monitors[c->mon].ww - c->m_left - c->m_right;
            c->h = monitors[c->mon].wh - c->m_top - c->m_bottom;
            manage_apply_gaps(c);
            manage_apply_size(c);
        }
    }
}

void
layout_tile(int m)
{
    struct Client *c;
    int i, num_clients, at_y, slave_h, master_w, master_n;

    /* Note: at_y, slave_h and master_w all count the *visible* sizes
     * including decorations */

    i = 0;
    num_clients = 0;
    at_y = monitors[m].wy;

    for (c = clients; c; c = c->next)
        if (VIS_ON_M(c, m) && !c->floating && !c->fullscreen)
            num_clients++;

    master_w = monitors[m].ww / 2;
    master_n = num_clients / 2;

    for (c = clients; c; c = c->next)
    {
        if (VIS_ON_M(c, m) && !c->floating && !c->fullscreen)
        {
            if (num_clients == 1)
            {
                /* Only one client total, just maximize it */
                c->x = monitors[m].wx + c->m_left;
                c->y = monitors[m].wy + c->m_top;
                c->w = monitors[m].ww - c->m_left - c->m_right;
                c->h = monitors[m].wh - c->m_top - c->m_bottom;
            }
            else
            {
                /* Reset at_y on column switch */
                if (i == master_n)
                    at_y = monitors[m].wy;

                /* Decide which column to place this client into */
                if (i < master_n)
                {
                    c->x = monitors[m].wx;
                    c->w = master_w;
                }
                else
                {
                    c->x = monitors[m].wx + master_w;
                    c->w = monitors[m].ww - master_w;
                }

                c->x += c->m_left;
                c->w -= c->m_left + c->m_right;

                c->y = at_y + c->m_top;

                /* Clients in the last row get the remaining space in
                 * order to avoid rounding issues. Note that we need to
                 * add monitors[m].wy here because that's where at_y
                 * started.
                 *
                 * Regular clients in the master or slave column get
                 * their normal share of available space. */
                if (i == num_clients - 1 || i == master_n - 1)
                    slave_h = monitors[m].wh - at_y + monitors[m].wy;
                else if (i < master_n)
                    slave_h = monitors[m].wh / master_n;
                else
                    slave_h = monitors[m].wh / (num_clients - master_n);

                c->h = slave_h - c->m_top - c->m_bottom;
                at_y += slave_h;
            }

            manage_apply_gaps(c);
            manage_apply_size(c);
            i++;
        }
    }
}

void
manage(Window win, XWindowAttributes *wa)
{
    struct Client *c, *tc;
    Window transient_for;
    size_t i;
    unsigned long wm_state[2];

    if (client_get_for_window(win))
    {
        fprintf(stderr, __NAME_WM__": Window %lu is already known, won't map\n",
                win);
        return;
    }

    c = ecalloc(1, sizeof (struct Client));

    c->win = win;
    c->mon = selmon;
    c->workspace = monitors[selmon].active_workspace;

    for (i = 0; i < SAVE_SLOTS; i++)
    {
        c->saved_monitors[i] = -1;
        c->saved_workspaces[i] = -1;
    }

    c->x = wa->x;
    c->y = wa->y;
    c->w = wa->width;
    c->h = wa->height;
    manage_set_decorations(c, true);

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
        c->floating = true;

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

    /* Evaluate various hints.
     *
     * Note that we inhibit rearrangement here because the client is not
     * yet saved. And we call manage_arrange() at the end of this
     * function anyway. */
    manage_ewmh_evaluate_hints(c);
    manage_icccm_evaluate_hints(c);
    manage_motif_evaluate_hints(c, false);
    manage_apply_rules(c);

    D fprintf(stderr, __NAME_WM__": Client %p lives on WS %d on monitor %d\n",
              (void *)c, c->workspace, c->mon);

    manage_fit_on_monitor(c);

    /* When a window spawns "in the background", we put it into hidden
     * state */
    if (c->workspace != monitors[c->mon].active_workspace)
    {
        D fprintf(stderr, __NAME_WM__": Client %p spawned in background, hiding\n",
                  (void *)c);
        manage_hide(c);
    }

    client_save(c);

    /* Whatever happens, we must add the client to the focus list, even
     * if it is not visible. Otherwise, *_first_matching() can never
     * find the client when the user finally wants to select it. */
    manage_focus_add_tail(c);

    D fprintf(stderr, __NAME_WM__": Managing window %lu (%p) at %dx%d+%d+%d\n",
              c->win, (void *)c, c->w, c->h, c->x, c->y);

    manage_arrange(c->mon);
    decorations_map(c);
    XMapWindow(dpy, c->win);
    manage_raisefocus(c);

    /* ICCCM 4.1.3.1 says that the WM should place a WM_STATE property
     * on client windows. We only ever manage windows in NormalState.
     * Note that wm_state also contains a window ID (of a potentially
     * existing icon window) which is always None for us -- but that's
     * why nelements = 2. */
    wm_state[0] = NormalState;
    wm_state[1] = None;
    XChangeProperty(dpy, c->win, atom_wm[AtomWMState], atom_wm[AtomWMState],
                    32, PropModeReplace, (unsigned char *)wm_state, 2);
}

void
manage_apply_gaps(struct Client *c)
{
    c->x += gap_pixels;
    c->y += gap_pixels;
    c->w -= 2 * gap_pixels;
    c->h -= 2 * gap_pixels;
}

void
manage_apply_rules(struct Client *c)
{
    size_t i, class_s, insta_s;
    XClassHint ch;
    char *class, *insta;

    D fprintf(stderr, __NAME_WM__": rules: Testing client %p\n", (void *)c);

    if (XGetClassHint(dpy, c->win, &ch))
    {
        D fprintf(stderr, __NAME_WM__": rules: Looking at class hints of "
                  "client %p\n", (void *)c);

        for (i = 0; i < sizeof rules / sizeof rules[0]; i++)
        {
            D fprintf(stderr, __NAME_WM__": rules: Testing %lu for %p\n", i,
                      (void *)c);
            D fprintf(stderr, __NAME_WM__": rules: '%s', '%s' vs. '%s', '%s'\n",
                      ch.res_class, ch.res_name,
                      rules[i].class, rules[i].instance);

            /* A window matches a rule if class and instance match. If
             * the rule's class or instance is NULL, then this field
             * matches everything. (strncmp returns 0 if n is 0 because
             * an empty string matches any "other" empty string.) */

            class = ch.res_class ? ch.res_class : "";
            insta = ch.res_name ? ch.res_name : "";

            class_s = rules[i].class ? strlen(rules[i].class) : 0;
            insta_s = rules[i].instance ? strlen(rules[i].instance) : 0;

            if (strncmp(class, rules[i].class, class_s) == 0 &&
                strncmp(insta, rules[i].instance, insta_s) == 0)
            {
                D fprintf(stderr, __NAME_WM__": rules: %lu matches for %p\n", i,
                          (void *)c);

                if (rules[i].workspace != -1)
                    c->workspace = rules[i].workspace;

                if (rules[i].monitor >= 0 && rules[i].monitor < monitors_num)
                    c->mon = rules[i].monitor;

                if (rules[i].floating != -1)
                    c->floating = rules[i].floating;

                break;
            }
        }

        if (ch.res_class)
            XFree(ch.res_class);
        if (ch.res_name)
            XFree(ch.res_name);
    }
}

void
manage_apply_size(struct Client *c)
{
    if (c->w < winsize_min_w || c->h < winsize_min_h)
        fprintf(stderr, __NAME_WM__": manage_apply_size(): w or h "
                "<= winsize_min_* (client size %dx%d, winsize_min %dx%d)\n",
                c->w, c->h, winsize_min_w, winsize_min_h);

    c->w = c->w < winsize_min_w ? winsize_min_w : c->w;
    c->h = c->h < winsize_min_h ? winsize_min_h : c->h;

    manage_icccm_apply_size_hints(c);

    if ((!c->hidden && c->fullscreen) || c->undecorated)
    {
        D fprintf(stderr, __NAME_WM__": Hiding deco of client %p\n", (void *)c);

        XMoveResizeWindow(dpy, c->decwin[DecWinTop], -15, 0, 10, 10);
        XMoveResizeWindow(dpy, c->decwin[DecWinLeft], -15, 0, 10, 10);
        XMoveResizeWindow(dpy, c->decwin[DecWinRight], -15, 0, 10, 10);
        XMoveResizeWindow(dpy, c->decwin[DecWinBottom], -15, 0, 10, 10);
    }
    else
    {
        D fprintf(stderr, __NAME_WM__": Moving client %p to %d, %d with size "
                  "%d, %d\n", (void *)c, c->x, c->y, c->w, c->h);

        XMoveResizeWindow(dpy, c->decwin[DecWinTop],
                          c->x - c->m_left, c->y - c->m_top,
                          c->m_left + c->w + c->m_right,
                          c->m_top);
        XMoveResizeWindow(dpy, c->decwin[DecWinLeft],
                          c->x - c->m_left, c->y,
                          c->m_left, c->h);
        XMoveResizeWindow(dpy, c->decwin[DecWinRight],
                          c->x + c->w, c->y,
                          c->m_right, c->h);
        XMoveResizeWindow(dpy, c->decwin[DecWinBottom],
                          c->x - c->m_left, c->y + c->h,
                          c->m_left + c->w + c->m_right,
                          c->m_bottom);
    }

    XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
}

void
manage_arrange(int m)
{
    D fprintf(stderr, __NAME_WM__": Arranging monitor %d\n", m);
    layouts[monitors[m].layouts[monitors[m].active_workspace]](m);

    publish_state();
}

void
manage_clear_urgency(struct Client *c)
{
    XWMHints *wmh;

    c->urgent = false;
    if ((wmh = XGetWMHints(dpy, c->win)))
    {
        wmh->flags &= ~XUrgencyHint;
        XSetWMHints(dpy, c->win, wmh);
        XFree(wmh);
    }
}

void
manage_client_gone(struct Client *c, bool rearrange)
{
    struct Client **tc;

    D fprintf(stderr, __NAME_WM__": No longer managing window %lu (%p)\n",
              c->win, (void *)c);

    /* Remove client from "clients" list, props for this neat little
     * loop go to dwm */
    for (tc = &clients; *tc && *tc != c; tc = &(*tc)->next);
    *tc = c->next;

    decorations_destroy(c);

    /* Remove client from focus list. Note that manage_focus_remove()
     * changes "focus". */
    manage_focus_remove(c);

    if (rearrange)
    {
        /* There are the following possibilites:
         *
         *   1. The destroyed window had focus and VIS_ON_SELMON(c) is
         *      true. The right thing to do is to focus the next window
         *      in our focus history.
         *   2. The destroyed window did not have focus and
         *      VIS_ON_SELMON(c) is true. Now, the currently focused
         *      window shall retain focus. The call below does not
         *      violate this requirement because the currently focused
         *      client is equivalent to the first matching client (if
         *      neither monitor nor workspace have been changed).
         *   3. VIS_ON_SELMON(c) is false. Implicitly, the destroyed
         *      window did not have input focus. We must now do the same
         *      thing as in #2.
         *
         * As always, do this before moving windows around. This is
         * especially important in this function because when a window
         * is gone, focus reverts back to the root window. If we now
         * move windows around, we could accidentally move a window
         * below the mouse pointer -- this would generate an EnterNotify
         * event with "focus YES" which confuses some applications.
         */
        manage_raisefocus_first_matching();
        manage_arrange(c->mon);
    }

    /* Once the client is gone, ICCCM 4.1.3.1 says that we can remove
     * the WM_STATE property */
    XDeleteProperty(dpy, c->win, atom_wm[AtomWMState]);

    free(c);

    publish_state();
}

void
manage_ewmh_evaluate_hints(struct Client *c)
{
    Atom prop, da;
    unsigned char *prop_ret = NULL;
    int di;
    unsigned long dl, ni;
    char *an;
    bool make_fullscreen = false;

    if (XGetWindowProperty(dpy, c->win, atom_net[AtomNetWMWindowType], 0,
                           sizeof (Atom), False, XA_ATOM, &da, &di, &dl, &dl,
                           &prop_ret)
        == Success)
    {
        if (prop_ret)
        {
            prop = ((Atom *)prop_ret)[0];
            if (prop == atom_net[AtomNetWMWindowTypeDialog] ||
                prop == atom_net[AtomNetWMWindowTypeMenu] ||
                prop == atom_net[AtomNetWMWindowTypeSplash] ||
                prop == atom_net[AtomNetWMWindowTypeToolbar] ||
                prop == atom_net[AtomNetWMWindowTypeUtility])
            {
                c->floating = true;
                an = XGetAtomName(dpy, prop);
                D fprintf(stderr, __NAME_WM__": Client %p should be floating, "
                          "says EWMH (has type %s)\n", (void *)c, an);
                if (an)
                    XFree(an);
            }
            else
                D fprintf(stderr, __NAME_WM__": Client %p has EWMH type, but "
                          "we don't know that type\n", (void *)c);

            XFree(prop_ret);
        }
        else
            D fprintf(stderr, __NAME_WM__": Client %p has EWMH type, but "
                      "pointer NULL\n", (void *)c);
    }
    else
        D fprintf(stderr, __NAME_WM__": Client %p has no EWMH window type\n",
                  (void *)c);

    /* If a client is not yet fullscreen, we check if _NET_WM_STATE asks
     * for a fullscreen window. 12 atoms are currently defined as valid
     * atoms for _NET_WM_STATE, so we ask for a list of max. 32 items.
     * IIUC, the fullscreen atom can appear anywhere in the list. (dwm
     * only check's the first item and I've never had problems with
     * that...) */
    if (!c->fullscreen)
    {
        if (XGetWindowProperty(dpy, c->win, atom_net[AtomNetWMState], 0,
                               32 * sizeof (Atom), False, XA_ATOM, &da, &di, &ni,
                               &dl, &prop_ret)
            == Success)
        {
            if (prop_ret)
            {
                while (ni--)
                {
                    prop = ((Atom *)prop_ret)[ni];

                    an = XGetAtomName(dpy, prop);
                    D fprintf(stderr, __NAME_WM__": Client %p has EWMH state: %s\n",
                              (void *)c, an);
                    if (an)
                        XFree(an);

                    if (prop == atom_net[AtomNetWMStateFullscreen])
                    {
                        make_fullscreen = true;

                        /* We found a fullscreen hint, okay, fine. Now
                         * stop looking for other stuff since we don't
                         * support it anyway. */
                        break;
                    }
                }
                XFree(prop_ret);
            }
        }

        if (make_fullscreen)
            manage_fullscreen(c);
    }
}

void
manage_fit_on_monitor(struct Client *c)
{
    /* Fit monitor on its screen in such a way that the upper left
     * corner is visible */

    if (c->fullscreen)
        return;

    /* Right and bottom */
    if (c->x + c->w + c->m_right + gap_pixels >=
        monitors[c->mon].wx + monitors[c->mon].ww)
    {
        c->x = monitors[c->mon].wx + monitors[c->mon].ww - c->w -
               c->m_right - gap_pixels;
    }

    if (c->y + c->h + c->m_bottom + gap_pixels >=
        monitors[c->mon].wy + monitors[c->mon].wh)
    {
        c->y = monitors[c->mon].wy + monitors[c->mon].wh - c->h -
               c->m_bottom - gap_pixels;
    }

    /* Top and left */
    if (c->x - c->m_left - gap_pixels < monitors[c->mon].wx)
        c->x = monitors[c->mon].wx + c->m_left + gap_pixels;
    if (c->y - c->m_top - gap_pixels < monitors[c->mon].wy)
        c->y = monitors[c->mon].wy + c->m_top + gap_pixels;

    manage_apply_size(c);
}

void
manage_focus_add_head(struct Client *new_focus)
{
    /* Add client to head of the focus list */

    new_focus->focus_next = focus;
    focus = new_focus;
}

void
manage_focus_add_tail(struct Client *nc)
{
    struct Client *c, *last = NULL;

    /* Add client to tail of the focus list */

    D
    {
        fprintf(stderr, __NAME_WM__": Focus list (pre tail-add): ");
        for (c = focus; c; c = c->focus_next)
            fprintf(stderr, "%p (%p) ", (void *)c,
                    (void *)(c ? c->focus_next : NULL));
        fprintf(stderr, "\n");
    }

    for (c = focus; c; c = c->focus_next)
        last = c;

    D fprintf(stderr, __NAME_WM__": Last client in focus list: %p\n",
              (void *)c);

    /* If this is the very first client added to the list, then we must
     * also set focus */
    if (last == NULL)
        focus = nc;
    else
        last->focus_next = nc;

    nc->focus_next = NULL;

    D
    {
        fprintf(stderr, __NAME_WM__": Focus list (post tail-add): ");
        for (c = focus; c; c = c->focus_next)
            fprintf(stderr, "%p (%p) ", (void *)c,
                    (void *)(c ? c->focus_next : NULL));
        fprintf(stderr, "\n");
    }
}

void
manage_focus_remove(struct Client *new_focus)
{
    bool found = false;
    struct Client **tc, *c;

    /* Remove client from focus list (if present) */

    for (c = focus; !found && c; c = c->focus_next)
        if (c == new_focus)
            found = true;

    if (!found)
        return;

    for (tc = &focus; *tc && *tc != new_focus; tc = &(*tc)->focus_next);
    *tc = new_focus->focus_next;
}

void
manage_focus_set(struct Client *new_focus)
{
    struct Client *c, *old_focus;

    D fprintf(stderr, __NAME_WM__": focus before list manipulation: %p (%p)\n",
              (void *)focus, (void *)(focus ? focus->focus_next : NULL));
    D fprintf(stderr, __NAME_WM__": new_focus before list manipulation: %p (%p)\n",
              (void *)new_focus, (void *)(new_focus ? new_focus->focus_next : NULL));

    old_focus = focus;

    if (new_focus)
    {
        /* Move newly selected client to head of focus list, thus
         * changing "focus" */

        D
        {
            fprintf(stderr, __NAME_WM__": Focus list (pre remove): ");
            for (c = focus; c; c = c->focus_next)
                fprintf(stderr, "%p (%p) ", (void *)c,
                        (void *)(c ? c->focus_next : NULL));
            fprintf(stderr, "\n");
        }

        manage_focus_remove(new_focus);

        D
        {
            fprintf(stderr, __NAME_WM__": Focus list (pre add): ");
            for (c = focus; c; c = c->focus_next)
                fprintf(stderr, "%p (%p) ", (void *)c,
                        (void *)(c ? c->focus_next : NULL));
            fprintf(stderr, "\n");
        }

        manage_focus_add_head(new_focus);
    }

    D
    {
        fprintf(stderr, __NAME_WM__": Focus list: ");
        for (c = focus; c; c = c->focus_next)
            fprintf(stderr, "%p (%p) ", (void *)c,
                    (void *)(c ? c->focus_next : NULL));
        fprintf(stderr, "\n");
    }

    /* Unfocus previous client, focus new client */
    if (old_focus)
        decorations_draw(old_focus, DecWinLAST);

    if (new_focus)
        decorations_draw(new_focus, DecWinLAST);
}

void
manage_fullscreen(struct Client *c)
{
    if (!c->fullscreen)
    {
        c->fullscreen = true;

        c->normal_x = c->x;
        c->normal_y = c->y;
        c->normal_w = c->w;
        c->normal_h = c->h;

        c->x = monitors[c->mon].mx;
        c->y = monitors[c->mon].my;
        c->w = monitors[c->mon].mw;
        c->h = monitors[c->mon].mh;

        /* We only support the state "fullscreen", so it's okay-ish to
         * only ever set this property (and kill all others) */
        XChangeProperty(dpy, c->win, atom_net[AtomNetWMState], XA_ATOM,
                        32, PropModeReplace,
                        (unsigned char *)&atom_net[AtomNetWMStateFullscreen], 1);

        manage_apply_size(c);
    }
}

void
manage_goto_monitor(int i, bool force)
{
    if (!force && selmon == i)
        return;

    if (i < 0 || i >= monitors_num)
        return;

    prevmon = selmon;
    selmon = i;

    manage_raisefocus_first_matching();
    XWarpPointer(dpy, None, root, 0, 0, 0, 0,
                 monitors[selmon].wx + monitors[selmon].ww / 2,
                 monitors[selmon].wy + monitors[selmon].wh / 2);

    publish_state();
}

void
manage_goto_workspace(int i, bool force)
{
    struct Client *c;

    i = i < WORKSPACE_MIN ? WORKSPACE_MIN : i;
    i = i > WORKSPACE_MAX ? WORKSPACE_MAX : i;

    if (!force && monitors[selmon].active_workspace == i)
        return;

    D fprintf(stderr, __NAME_WM__": Changing to workspace %d\n", i);

    monitors[selmon].recent_workspace = monitors[selmon].active_workspace;
    monitors[selmon].active_workspace = i;

    /* Before moving windows around, transfer input focus to the correct
     * window. This avoids creating EnterNotify events before FocusIn
     * events. */
    manage_raisefocus_first_matching();

    /* First make new clients visible (and re-fit any floaters in the
     * process), then hide old clients. This way, the root window won't
     * be visible for a tiny fraction of time, hence we get less
     * flickering. */
    for (c = clients; c; c = c->next)
        if (c->mon == selmon && c->workspace == i)
        {
            manage_show(c);
            manage_fit_on_monitor(c);
        }

    for (c = clients; c; c = c->next)
        if (c->mon == selmon && c->workspace != i)
            manage_hide(c);

    manage_arrange(selmon);
}

void
manage_hide(struct Client *c)
{
    if (!c->hidden)
    {
        c->nonhidden_x = c->x;
        c->x = -2 * c->w;
        c->hidden = true;

        manage_apply_size(c);
    }
}

void
manage_icccm_apply_size_hints(struct Client *c)
{
    bool base_supplied, base_subtracted = false;
    double aspect;

    if (!SOMEHOW_FLOATING(c) || c->fullscreen)
        return;

    /* Apply ICCCM 4.1.2.3 size hints. Again, this is taken almost
     * verbatim from dwm (except for aspect ratios). */

    base_supplied = !(c->sh_base_w == c->sh_min_w && c->sh_base_h == c->sh_min_h);

    if (base_supplied)
    {
        /* Remove base sizes when looking at aspect ratios, if they were
         * specified. ICCCM says not to do this if we stored min sizes
         * as fallback. */
        c->w -= c->sh_base_w;
        c->h -= c->sh_base_h;
        base_subtracted = true;
    }
    if (c->sh_asp_min > 0 && c->sh_asp_max > 0)
    {
        /* Honour aspect ratio request by simply adjusting the window's
         * width */
        aspect = (double)c->w / c->h;
        if (aspect < c->sh_asp_min)
            c->w = c->h * c->sh_asp_min;
        else if (aspect > c->sh_asp_max)
            c->w = c->h * c->sh_asp_max;
    }

    if (!base_subtracted)
    {
        /* If we have not yet subtracted the base size, then we must do
         * so now for increment calculation to work */
        c->w -= c->sh_base_w;
        c->h -= c->sh_base_h;
    }
    if (c->sh_inc_w > 0)
        c->w -= c->w % c->sh_inc_w;
    if (c->sh_inc_h > 0)
        c->h -= c->h % c->sh_inc_h;

    /* Restore base dimensions and enforce size constraints (min_* is
     * always >= 0 and that's fine, but we must check whether max_* has
     * been specified) */
    c->w += c->sh_base_w;
    c->h += c->sh_base_h;

    c->w = c->w < c->sh_min_w ? c->sh_min_w : c->w;
    c->h = c->h < c->sh_min_h ? c->sh_min_h : c->h;

    if (c->sh_max_w > 0)
        c->w = c->w > c->sh_max_w ? c->sh_max_w : c->w;
    if (c->sh_max_h > 0)
        c->h = c->h > c->sh_max_h ? c->sh_max_h : c->h;
}

void
manage_icccm_evaluate_hints(struct Client *c)
{
    XWMHints *wmh;
    XSizeHints xsh;
    long dl;

    if ((wmh = XGetWMHints(dpy, c->win)))
    {
        if (wmh->flags & XUrgencyHint)
        {
            if (c == focus && VIS_ON_SELMON(c))
            {
                /* Setting the urgency hint on the currently selected
                 * window shall have no effect */
                wmh->flags &= ~XUrgencyHint;
                XSetWMHints(dpy, c->win, wmh);
                D fprintf(stderr, __NAME_WM__": Urgency hint on client "
                          "%p ignored because selected\n", (void *)c);
            }
            else
            {
                c->urgent = true;
                decorations_draw(c, DecWinLAST);
                D fprintf(stderr, __NAME_WM__": Urgency hint on client "
                          "%p set\n", (void *)c);
            }
        }
        else if (c->urgent)
        {
            /* Urgency hint has been cleared by the application */
            c->urgent = false;
            decorations_draw(c, DecWinLAST);
            D fprintf(stderr, __NAME_WM__": Urgency hint on client %p "
                      "cleared\n", (void *)c);
        }

        /* This is from a dwm patch by Brendan MacDonell:
         * http://lists.suckless.org/dev/1104/7548.html
         * It takes care of ICCCM input focus models. */
        if (wmh->flags & InputHint)
        {
            c->never_focus = !wmh->input;
            D fprintf(stderr, __NAME_WM__": Client %p never_focus: %d\n",
                      (void *)c, c->never_focus);
        }
        else
        {
            c->never_focus = false;
            D fprintf(stderr, __NAME_WM__": Client %p never_focus: %d\n",
                      (void *)c, c->never_focus);
        }

        publish_state();
        XFree(wmh);
    }

    if (XGetWMNormalHints(dpy, c->win, &xsh, &dl))
    {
        /* Store the client's size hints according to ICCCM 4.1.2.3.
         * This is pretty much taken from dwm, but there's virtually no
         * other way to do this anyway. */
        if (xsh.flags & PBaseSize)
        {
            c->sh_base_w = xsh.base_width;
            c->sh_base_h = xsh.base_height;
        }
        else if (xsh.flags & PMinSize)
        {
            c->sh_base_w = xsh.min_width;
            c->sh_base_h = xsh.min_height;
        }
        else
            c->sh_base_w = c->sh_base_h = 0;

        if (xsh.flags & PResizeInc)
        {
            c->sh_inc_w = xsh.width_inc;
            c->sh_inc_h = xsh.height_inc;
        }
        else
            c->sh_inc_w = c->sh_inc_h = 0;

        if (xsh.flags & PMaxSize)
        {
            c->sh_max_w = xsh.max_width;
            c->sh_max_h = xsh.max_height;
        }
        else
            c->sh_max_w = c->sh_max_h = 0;

        if (xsh.flags & PMinSize)
        {
            c->sh_min_w = xsh.min_width;
            c->sh_min_h = xsh.min_height;
        }
        else if (xsh.flags & PBaseSize)
        {
            c->sh_min_w = xsh.base_width;
            c->sh_min_h = xsh.base_height;
        }
        else
            c->sh_min_w = c->sh_min_h = 0;

        if (xsh.flags & PAspect)
        {
            c->sh_asp_min = (double)xsh.min_aspect.x / xsh.min_aspect.y;
            c->sh_asp_max = (double)xsh.max_aspect.x / xsh.max_aspect.y;
        }
        else
            c->sh_asp_max = c->sh_asp_min = 0.0;

        /* Support "fixed sized windows". Dialogs often set both min and
         * max size to the same value, indicating they want a window of
         * a certain size. We honour that request and resize the window.
         * */
        if (xsh.flags & PMinSize && xsh.flags & PMaxSize &&
            xsh.min_width == xsh.max_width && xsh.min_height == xsh.max_height)
        {
            D fprintf(stderr, __NAME_WM__": Client %p requested a fixed size "
                      "of %dx%d\n", (void *)c, xsh.min_width, xsh.min_height);

            c->floating = true;
            c->w = xsh.min_width;
            c->h = xsh.min_height;
            manage_apply_size(c);
        }
    }
}

void
manage_motif_evaluate_hints(struct Client *c, bool rearrange)
{
    Atom da;
    unsigned long *prop_ret = NULL;
    int di;
    unsigned long dl, ni;

    /* Check _MOTIF_WM_HINTS for the "decorations" byte.
     *
     * That "5" is PROP_MOTIF_WM_HINTS_ELEMENTS in MwmUtil.h. Once we
     * got the property, we need to check the third byte
     * (PropMwmHints.decorations). */
    if (XGetWindowProperty(dpy, c->win, atom_motif, 0, 5, False, atom_motif,
                           &da, &di, &ni, &dl, (unsigned char **)&prop_ret)
        == Success)
    {
        if (ni >= 3 && prop_ret)
        {
            if (prop_ret[2] == 0)
            {
                D fprintf(stderr, __NAME_WM__": MWM hints indicate undecorated "
                          "window for client %p\n", (void *)c);
                manage_set_decorations(c, false);
            }
            else
            {
                D fprintf(stderr, __NAME_WM__": MWM hints indicate normal "
                          "window decorations for client %p\n", (void *)c);
                manage_set_decorations(c, true);
            }
        }

        if (prop_ret)
            XFree(prop_ret);
    }

    /* Move window decorations away now, or restore them. This only
     * really matters for floating clients and only when the property
     * changes, because for non-floaters or newly mapped clients,
     * manage_arrange() is usually called afterwards. */
    manage_apply_size(c);

    if (rearrange)
        manage_arrange(c->mon);
}

void
manage_raisefocus(struct Client *c)
{
    if (c && !VIS_ON_SELMON(c))
    {
        D fprintf(stderr, __NAME_WM__": Client %p should have been "
                  "focused/raised, but it's not currently visible. Ignoring.\n",
                  (void *)c);
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

    for (c = focus; c; c = c->focus_next)
    {
        if (VIS_ON_SELMON(c))
        {
            manage_raisefocus(c);
            return;
        }
    }

    /* If we end up here, then no client has been found on the target
     * monitor. Still, due to the fact that we changed the monitor (or
     * workspace or ...), we must now unfocus the previously selected
     * client. */
    manage_raisefocus(NULL);
}

void
manage_set_decorations(struct Client *c, bool decorated)
{
    if (decorated)
    {
        c->undecorated = false;
        c->m_top = dgeo.top_height;
        c->m_left = dgeo.left_width;
        c->m_right = dgeo.right_width;
        c->m_bottom = dgeo.bottom_height;
    }
    else
    {
        c->undecorated = true;
        c->m_top = 0;
        c->m_left = 0;
        c->m_right = 0;
        c->m_bottom = 0;
    }
}

void
manage_show(struct Client *c)
{
    if (c->hidden)
    {
        c->x = c->nonhidden_x;
        c->hidden = false;

        manage_apply_size(c);
    }
}

void
manage_unfullscreen(struct Client *c)
{
    if (c->fullscreen)
    {
        c->fullscreen = false;

        c->x = c->normal_x;
        c->y = c->normal_y;
        c->w = c->normal_w;
        c->h = c->normal_h;

        /* XXX dwm empties the list instead of simply removing the
         * property. Why is that? */
        XDeleteProperty(dpy, c->win, atom_net[AtomNetWMState]);

        manage_apply_size(c);

        /* Scenario: You use the tiling layout, several clients are
         * visible right now. You put one client to fullscreen. You
         * switch to another workspace, then switch back. The full-
         * screened client is now visible. You un-fullscreen it, so it
         * snaps back into its previous position.
         *
         * Problem is, that previous position might not be up to date
         * anymore, because the number of tiled clients might have
         * changed. Moreover, due to the change of workspaces,
         * manage_arrange() has been called in the meantime -- this call
         * ignored the fullscreen client, of course, so another client
         * has taken over the space which was previously occupied by our
         * client right here ...
         *
         * Long story short, we have to rearrange the current workspace. */
        manage_arrange(selmon);
    }
}

void
manage_xfocus(struct Client *c)
{
    if (c)
    {
        manage_clear_urgency(c);
        publish_state();

        if (!c->never_focus)
        {
            /* XXX This leads to EnterNotify events with "focus
             * YES" and I don't think there is a fix.
             *
             * Here's the scenario. Window B overlaps window A. The
             * position of window C does not matter. The focus history
             * is "B, C, A", so when window B dies, we are going to
             * focus window C and window A is NOT to receive focus.
             *
             * Now move the mouse pointer to a position inside of window
             * B where, would we remove window B, the mouse would be
             * inside of window A (remember, the two windows overlap).
             *
             * Now close window B. We get an UnmapNotify event and move
             * focus to window C. In the meantime, however, window A
             * received an EnterNotify event because the mouse is now
             * inside of its area. That's correct. However, that
             * EnterNotify event also says "focus YES" which some
             * applications could interpret as "I AM FOCUSED". Even
             * though we do transfer the focus afterwards and the
             * applications in question get a FocusOut event, said
             * applications might be confused enough not to "respond" to
             * that FocusOut event. They might still assume to be
             * focused.
             *
             * There is no way to tell the X server to revert the input
             * focus to *one particular window* ("nofocus" in our case)
             * when a window dies. I speculate that this is a corner
             * case where we would *need* reparenting: We could then
             * revert the focus to the parent window of the dying window
             * which is *our* frame window. Now, we would be able to
             * properly transfer the focus to another window. After
             * that, we can kill the frame window. Now finally, window A
             * does still receive an EnterNotify event but with "focus
             * NO". */
            XSetInputFocus(dpy, c->win, RevertToParent, CurrentTime);
        }

        manage_xsend_icccm(c, atom_wm[AtomWMTakeFocus]);
    }
    else
        XSetInputFocus(dpy, nofocus, RevertToParent, CurrentTime);
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

int
manage_xsend_icccm(struct Client *c, Atom atom)
{
    /* This is from a dwm patch by Brendan MacDonell:
     * http://lists.suckless.org/dev/1104/7548.html */

    int n;
    Atom *protocols;
    int exists = 0;
    XEvent ev;

    if (XGetWMProtocols(dpy, c->win, &protocols, &n))
    {
        while (!exists && n--)
            exists = protocols[n] == atom;
        XFree(protocols);
    }
    if (exists)
    {
        ev.type = ClientMessage;
        ev.xclient.window = c->win;
        ev.xclient.message_type = atom_wm[AtomWMProtocols];
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = atom;
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(dpy, c->win, False, NoEventMask, &ev);

        D fprintf(stderr, __NAME_WM__": ICCCM: Atom %lu sent to client %p\n",
                  atom, (void *)c);
    }
    else
        D fprintf(stderr, __NAME_WM__": ICCCM: Atom NOT %lu sent to client %p\n",
                  atom, (void *)c);
    return exists;
}

void
publish_state(void)
{
    size_t size, off, byte_i, shifts_needed, i, size_monws;
    unsigned char *state = NULL, byte, mask;
    int m;
    struct Client *c;

    /* The very first byte indicates the number of monitors detected by
     * us. The second byte indicates the index of the currently selected
     * monitor. The third byte is a bitmask where each bit indicates
     * whether the corresponding save slot is occupied. Then, the next
     * monitors_num bytes indicate the active workspace on each monitor.
     * The next monitors_num bytes indicate the active layout on each
     * monitor (note: different layouts might be active on different
     * workspaces on each monitor, but they are not visible anyway, so
     * they're not included). Following that, we need WORKSPACE_MAX / 8
     * = ~16 bytes per monitor to indicate whether that workspace is
     * occupied. We need the same amount of data to indicate whether a
     * workspace has the urgency hint set. */

    size_monws = STATE_BYTES_PER_WORKSPACE;

    size = 1 + 1 + 1 + monitors_num * 2 + monitors_num * size_monws * 2;
    state = ecalloc(size, sizeof (unsigned char));

    /* Number of detected monitors and currently selected monitor (int) */
    state[0] = monitors_num;
    state[1] = selmon;
    off = 2;

    /* Bitmask of occupied save slots */
    mask = 1;
    for (i = 0; i < SAVE_SLOTS; i++)
    {
        if (saved_monitors[i])
            state[off] |= mask;
        mask <<= 1;
    }
    off++;

    /* Active workspace on each monitor (int) */
    for (m = 0; m < monitors_num; m++)
        state[off + m] = monitors[m].active_workspace;
    off += monitors_num;

    /* Visible layout on each monitor (layout index as int) */
    for (m = 0; m < monitors_num; m++)
        state[off + m] = monitors[m].layouts[monitors[m].active_workspace];
    off += monitors_num;

    /* Bitmasks for occupied workspaces and urgent hints */
    for (c = clients; c; c = c->next)
    {
        /* Calculate which byte to alter and then which bit to set */

        byte_i = c->workspace / 8;
        shifts_needed = c->workspace % 8;

        mask = 1;
        for (i = 0; i < shifts_needed; i++)
            mask <<= 1;

        /* Occupied workspaces */
        i = off + c->mon * size_monws + byte_i;
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

    D
    {
        fprintf(stderr, __NAME_WM__": Published internal state in root property "
                "%s: ", IPC_ATOM_STATE);
        for (i = 0; i < size; i++)
            fprintf(stderr, "%d ", state[i]);
        fprintf(stderr, "\n");
    }
}

void
run(void)
{
    XEvent ev;

    while (running)
    {
        XNextEvent(dpy, &ev);
        D fprintf(stderr, __NAME_WM__": Event %d\n", ev.type);
        if (x11_handler[ev.type])
            x11_handler[ev.type](&ev);
    }
}

void
setup(void)
{
    size_t i;
    XSetWindowAttributes wa = { .override_redirect = True };

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
        D fprintf(stderr, __NAME_WM__": Loaded font '%s'\n", dec_fonts[i]);
    }
    for (i = DecStateNormal; i <= DecStateUrgent; i++)
    {
        if (!XftColorAllocName(dpy, DefaultVisual(dpy, screen),
                               DefaultColormap(dpy, screen), dec_font_colors[i],
                               &font_color[i]))
        {
            fprintf(stderr, __NAME_WM__": Cannot load color '%s'\n",
                    dec_font_colors[i]);
            exit(EXIT_FAILURE);
        }
        D fprintf(stderr, __NAME_WM__": Loaded color '%s'\n", dec_font_colors[i]);
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

    XWarpPointer(dpy, None, root, 0, 0, 0, 0,
                 monitors[selmon].wx + monitors[selmon].ww / 2,
                 monitors[selmon].wy + monitors[selmon].wh / 2);

    /* Create a window which will receive input focus when no real
     * window has focus. We do this to avoid any kind of "*PointerRoot"
     * usage. Focusing the root window confuses applications and kind of
     * returns to sloppy focus. */
    nofocus = XCreateSimpleWindow(dpy, root, -10, -10, 1, 1, 0, 0, 0);
    XChangeWindowAttributes(dpy, nofocus, CWOverrideRedirect, &wa);
    XMapWindow(dpy, nofocus);
    manage_xfocus(NULL);

    /* Set up _NET_SUPPORTING_WM_CHECK. */
    XChangeProperty(dpy, root, atom_net[AtomNetSupportingWMCheck], XA_WINDOW,
                    32, PropModeReplace, (unsigned char *)&nofocus, 1);
    XChangeProperty(dpy, nofocus, atom_net[AtomNetSupportingWMCheck], XA_WINDOW,
                    32, PropModeReplace, (unsigned char *)&nofocus, 1);
    XChangeProperty(dpy, nofocus, atom_net[AtomNetWMName],
                    XInternAtom(dpy, "UTF8_STRING", False), 8,
                    PropModeReplace, (unsigned char *)__NAME_WM__,
                    strlen(__NAME_WM__));

    publish_state();
}

void
setup_hints(void)
{
    atom_net[AtomNetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
    atom_net[AtomNetSupportingWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
    atom_net[AtomNetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
    atom_net[AtomNetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
    atom_net[AtomNetWMStateFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);

    atom_net[AtomNetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    atom_net[AtomNetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    atom_net[AtomNetWMWindowTypeMenu] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_MENU", False);
    atom_net[AtomNetWMWindowTypeSplash] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_SPLASH", False);
    atom_net[AtomNetWMWindowTypeToolbar] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_TOOLBAR", False);
    atom_net[AtomNetWMWindowTypeUtility] = XInternAtom( dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);

    XChangeProperty(dpy, root, atom_net[AtomNetSupported], XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)atom_net, AtomNetLAST);

    atom_wm[AtomWMDeleteWindow] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    atom_wm[AtomWMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
    atom_wm[AtomWMState] = XInternAtom(dpy, "WM_STATE", False);
    atom_wm[AtomWMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);

    atom_motif = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
}

int
setup_monitors_compare(const void *a, const void *b)
{
    const struct Monitor *ma, *mb;

    ma = (struct Monitor *)a;
    mb = (struct Monitor *)b;

    /* Sort monitors lemonbar/neeasade style */

    if (ma->mx < mb->mx || ma->my + ma->mh <= mb->my)
        return -1;

    if (ma->mx > mb->mx || ma->my + ma->mh > mb->my)
        return 1;

    return 0;
}

bool
setup_monitors_is_duplicate(XRRCrtcInfo *ci, bool *chosen, XRRScreenResources *sr)
{
    XRRCrtcInfo *o;
    int i;

    for (i = 0; i < sr->ncrtc; i++)
    {
        if (chosen[i])
        {
            o = XRRGetCrtcInfo(dpy, sr, sr->crtcs[i]);
            if (o->x == ci->x && o->y == ci->y &&
                o->width == ci->width && o->height == ci->height)
                return true;
        }
    }

    return false;
}

void
setup_monitors_read(void)
{
    XRRCrtcInfo *ci;
    XRRScreenResources *sr;
    int c, mi;
    size_t li;
    bool *chosen = NULL;

    sr = XRRGetScreenResources(dpy, root);
    D fprintf(stderr, __NAME_WM__": XRandR reported %d monitors/CRTCs\n",
              sr->ncrtc);
    assert(sr->ncrtc > 0);

    /* First, we iterate over all monitors and check each monitor if
     * it's usable and not a duplicate. If it's okay, we mark it for
     * use. After this loop, we know how many usable monitors there
     * are, so we can allocate the "monitors" array. */

    monitors_num = 0;
    chosen = ecalloc(sr->ncrtc, sizeof (bool));
    for (c = 0; c < sr->ncrtc; c++)
    {
        ci = XRRGetCrtcInfo(dpy, sr, sr->crtcs[c]);
        if (ci == NULL || ci->noutput == 0 || ci->mode == None)
            continue;

        if (setup_monitors_is_duplicate(ci, chosen, sr))
            continue;

        chosen[c] = true;
        monitors_num++;
    }

    monitors = ecalloc(monitors_num, sizeof (struct Monitor));
    mi = 0;
    for (c = 0; c < sr->ncrtc; c++)
    {
        if (chosen[c])
        {
            ci = XRRGetCrtcInfo(dpy, sr, sr->crtcs[c]);

            monitors[mi].wx = monitors[mi].mx = ci->x;
            monitors[mi].wy = monitors[mi].my = ci->y;
            monitors[mi].ww = monitors[mi].mw = ci->width;
            monitors[mi].wh = monitors[mi].mh = ci->height;

            monitors[mi].wx += wai.left;
            monitors[mi].ww -= wai.left + wai.right;
            monitors[mi].wy += wai.top;
            monitors[mi].wh -= wai.top + wai.bottom;

            mi++;
        }
    }
    free(chosen);

    qsort(monitors, monitors_num, sizeof (struct Monitor), setup_monitors_compare);

    for (mi = 0; mi < monitors_num; mi++)
    {
        monitors[mi].active_workspace = setup_monitors_wsdef(mi, monitors_num);
        monitors[mi].recent_workspace = monitors[mi].active_workspace;
        for (li = 0;
             li < sizeof monitors[mi].layouts / sizeof monitors[mi].layouts[0];
             li++)
        {
            monitors[mi].layouts[li] = default_layout;
        }
    }

    D
    {
        fprintf(stderr, __NAME_WM__": We found %d usable monitors\n", monitors_num);
        for (mi = 0; mi < monitors_num; mi++)
            fprintf(stderr, __NAME_WM__": monitor %d: %d %d, %d %d\n", mi,
                    monitors[mi].mx, monitors[mi].my,
                    monitors[mi].mw, monitors[mi].mh);
    }
}

int
setup_monitors_wsdef(int mi, int monitors_num)
{
    size_t i = 0;

    /* See explanation and examples in config.def.h */

    while (i < sizeof initial_workspaces / sizeof initial_workspaces[0])
    {
        if (initial_workspaces[i] == monitors_num)
            return initial_workspaces[i + 1 + mi];

        i += initial_workspaces[i];
    }

    return default_workspace;
}

void
scan(void)
{
    unsigned int i, num;
    Window d1, d2, *wins = NULL;
    XWindowAttributes wa;

    if (XQueryTree(dpy, root, &d1, &d2, &wins, &num))
    {
        D fprintf(stderr, __NAME_WM__": scan() saw %d windows\n", num);

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
shutdown_monitors_free(void)
{
    free(monitors);
    monitors = NULL;
    monitors_num = 0;
    selmon = 0;
    prevmon = 0;
}

int
xerror(Display *dpy, XErrorEvent *ee)
{
    /* Taken from dwm */

    /* There's no way to check accesses to destroyed windows, thus those
     * cases are ignored (especially on UnmapNotify's). Other types of
     * errors call Xlibs default error handler, which may call exit. */

    if (ee->error_code == BadWindow ||
        (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch) ||
        (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable) ||
        (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable) ||
        (ee->request_code == X_PolySegment && ee->error_code == BadDrawable) ||
        (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch) ||
        (ee->request_code == X_GrabButton && ee->error_code == BadAccess) ||
        (ee->request_code == X_GrabKey && ee->error_code == BadAccess) ||
        (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
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
    cleanup();

    if (restart)
        execvp(argv[0], argv);

    exit(EXIT_SUCCESS);
}
