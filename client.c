#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include "util.h"
#include "ipc.h"

static int fn_atoi(char *s, char d) { (void)d; return atoi(s ? s : ""); }
static int fn_int(char *d, char p) { (void)d; return p; }

struct Command
{
    char *ops[2];
    enum IPCCommand cmd;
    int (*handler)(char *argv, char payload);
    char payload;
};

#define ANY NULL
static struct Command c[] = {
    /* If you sort this list, make sure to sort in asciibetical order,
     * i.e. set LANG=C. Order matters, ANY would also match "next", for
     * example. */
    {  {  "client_close",              ANY        },  IPCClientClose,                    NULL,     0          },
    {  {  "client_fullscreen_toggle",  ANY        },  IPCClientFullscreenToggle,         NULL,     0          },
    {  {  "client_kill",               ANY        },  IPCClientKill,                     NULL,     0          },
    {  {  "client_move_list",          "next"     },  IPCClientMoveList,                 fn_int,   +1         },
    {  {  "client_move_list",          "prev"     },  IPCClientMoveList,                 fn_int,   -1         },
    {  {  "client_move_mouse",         "down"     },  IPCClientMoveMouse,                fn_int,   0          },
    {  {  "client_move_mouse",         "motion"   },  IPCClientMoveMouse,                fn_int,   1          },
    {  {  "client_move_mouse",         "up"       },  IPCClientMoveMouse,                fn_int,   2          },
    {  {  "client_resize_mouse",       "down"     },  IPCClientResizeMouse,              fn_int,   0          },
    {  {  "client_resize_mouse",       "motion"   },  IPCClientResizeMouse,              fn_int,   1          },
    {  {  "client_resize_mouse",       "up"       },  IPCClientResizeMouse,              fn_int,   2          },
    {  {  "client_select",             "next"     },  IPCClientSelectAdjacent,           fn_int,   +1         },
    {  {  "client_select",             "prev"     },  IPCClientSelectAdjacent,           fn_int,   -1         },
    {  {  "client_switch_monitor",     "left"     },  IPCClientSwitchMonitorAdjacent,    fn_int,   -1         },
    {  {  "client_switch_monitor",     "right"    },  IPCClientSwitchMonitorAdjacent,    fn_int,   +1         },
    {  {  "client_switch_workspace",   "next"     },  IPCClientSwitchWorkspaceAdjacent,  fn_int,   +1         },
    {  {  "client_switch_workspace",   "prev"     },  IPCClientSwitchWorkspaceAdjacent,  fn_int,   -1         },
    {  {  "client_switch_workspace",   ANY        },  IPCClientSwitchWorkspace,          fn_atoi,  0          },
    {  {  "layout_set",                "float"    },  IPCLayoutSet,                      fn_int,   LAFloat    },
    {  {  "layout_set",                "monocle"  },  IPCLayoutSet,                      fn_int,   LAMonocle  },
    {  {  "layout_set",                "tile"     },  IPCLayoutSet,                      fn_int,   LATile     },
    {  {  "monitor_select",            "left"     },  IPCMonitorSelectAdjacent,          fn_int,   -1         },
    {  {  "monitor_select",            "right"    },  IPCMonitorSelectAdjacent,          fn_int,   +1         },
    {  {  "wm_quit",                   ANY        },  IPCWMQuit,                         NULL,     0          },
    {  {  "wm_restart",                ANY        },  IPCWMRestart,                      NULL,     0          },
    {  {  "workspace_select",          "next"     },  IPCWorkspaceSelectAdjacent,        fn_int,   +1         },
    {  {  "workspace_select",          "prev"     },  IPCWorkspaceSelectAdjacent,        fn_int,   -1         },
    {  {  "workspace_select",          ANY        },  IPCWorkspaceSelect,                fn_atoi,  0          },
};

static int
send_command(enum IPCCommand cmd, char arg)
{
    Display *dpy;
    Window root;
    XEvent ev;

    if (!(dpy = XOpenDisplay(NULL)))
        return 0;

    root = DefaultRootWindow(dpy);

    memset(&ev, 0, sizeof ev);
    ev.xclient.type = ClientMessage;
    ev.xclient.window = root;

    /* This is our "protocol": One byte opcode, one byte argument */
    ev.xclient.message_type = XInternAtom(dpy, IPC_ATOM_COMMAND, False);
    ev.xclient.format = 8;
    ev.xclient.data.b[0] = cmd;
    ev.xclient.data.b[1] = arg;

    /* Send this message to all clients which have selected for
     * "SubstructureRedirectMask" on the root window. By definition,
     * this is the window manager. */
    DPRINTF(__NAME_C__": Sending cmd %d, arg %d\n", cmd, arg);
    XSendEvent(dpy, root, False, SubstructureRedirectMask, &ev);
    XSync(dpy, False);

    return 1;
}

int
main(int argc, char **argv)
{
    size_t i;
    enum IPCCommand cmd = IPCLast;
    char arg = 0;

    if (argc < 2)
    {
        fprintf(stderr, "Usage: "__NAME_C__" COMMAND [OPTION]\n");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < sizeof c / sizeof c[0]; i++)
    {
        if (strncmp(argv[1], c[i].ops[0], strlen(c[i].ops[0])) == 0)
        {
            if (c[i].ops[1] == ANY
                || (argc >= 3
                    && !strncmp(argv[2], c[i].ops[1], strlen(c[i].ops[1]))))
            {
                cmd = c[i].cmd;
                if (c[i].handler)
                    arg = c[i].handler(argc >= 3 ? argv[2] : NULL,
                                       c[i].payload);

                if (send_command(cmd, arg))
                    exit(EXIT_SUCCESS);
                else
                    exit(EXIT_FAILURE);
            }
        }
    }

    fprintf(stderr, __NAME_C__": Unknown command '%s'\n", argv[1]);
        exit(EXIT_FAILURE);
}
