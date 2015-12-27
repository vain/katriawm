#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include "util.h"
#include "ipc.h"

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
    enum IPCCommand cmd = IPCLast;
    char arg = 0;

    if (argc < 2)
    {
        fprintf(stderr, "Expected arguments\n");
        exit(EXIT_FAILURE);
    }

    if (strncmp(argv[1], "client_move_list", strlen("client_move_list")) == 0 && argc >= 3)
    {
        if (strncmp(argv[2], "prev", strlen("prev")) == 0)
        {
            cmd = IPCClientMoveList;
            arg = -1;
        }
        if (strncmp(argv[2], "next", strlen("next")) == 0)
        {
            cmd = IPCClientMoveList;
            arg = 1;
        }
    }
    if (strncmp(argv[1], "client_move_mouse", strlen("client_move_mouse")) == 0 && argc >= 3)
    {
        if (strncmp(argv[2], "down", strlen("down")) == 0)
        {
            cmd = IPCClientMoveMouse;
            arg = 0;
        }
        if (strncmp(argv[2], "motion", strlen("motion")) == 0)
        {
            cmd = IPCClientMoveMouse;
            arg = 1;
        }
        if (strncmp(argv[2], "up", strlen("up")) == 0)
        {
            cmd = IPCClientMoveMouse;
            arg = 2;
        }
    }
    if (strncmp(argv[1], "client_resize_mouse", strlen("client_resize_mouse")) == 0 && argc >= 3)
    {
        if (strncmp(argv[2], "down", strlen("down")) == 0)
        {
            cmd = IPCClientResizeMouse;
            arg = 0;
        }
        if (strncmp(argv[2], "motion", strlen("motion")) == 0)
        {
            cmd = IPCClientResizeMouse;
            arg = 1;
        }
        if (strncmp(argv[2], "up", strlen("up")) == 0)
        {
            cmd = IPCClientResizeMouse;
            arg = 2;
        }
    }
    if (strncmp(argv[1], "client_select", strlen("client_select")) == 0 && argc >= 3)
    {
        if (strncmp(argv[2], "prev", strlen("prev")) == 0)
        {
            cmd = IPCClientSelectAdjacent;
            arg = -1;
        }
        if (strncmp(argv[2], "next", strlen("next")) == 0)
        {
            cmd = IPCClientSelectAdjacent;
            arg = 1;
        }
    }
    if (strncmp(argv[1], "layout_set", strlen("layout_set")) == 0 && argc >= 3)
    {
        if (strncmp(argv[2], "float", strlen("float")) == 0)
        {
            cmd = IPCLayoutSet;
            arg = LAFloat;
        }
        if (strncmp(argv[2], "monocle", strlen("monocle")) == 0)
        {
            cmd = IPCLayoutSet;
            arg = LAMonocle;
        }
        if (strncmp(argv[2], "tile", strlen("tile")) == 0)
        {
            cmd = IPCLayoutSet;
            arg = LATile;
        }
    }
    if (strncmp(argv[1], "monitor_select", strlen("monitor_select")) == 0 && argc >= 3)
    {
        if (strncmp(argv[2], "left", strlen("left")) == 0)
        {
            cmd = IPCMonitorSelectAdjacent;
            arg = -1;
        }
        if (strncmp(argv[2], "right", strlen("right")) == 0)
        {
            cmd = IPCMonitorSelectAdjacent;
            arg = 1;
        }
    }
    if (strncmp(argv[1], "wm_restart", strlen("wm_restart")) == 0)
        cmd = IPCWMRestart;
    if (strncmp(argv[1], "wm_quit", strlen("wm_quit")) == 0)
        cmd = IPCWMQuit;
    if (strncmp(argv[1], "workspace_select", strlen("workspace_select")) == 0 && argc >= 3)
    {
        if (strncmp(argv[2], "prev", strlen("prev")) == 0)
        {
            cmd = IPCWorkspaceSelectAdjacent;
            arg = -1;
        }
        else if (strncmp(argv[2], "next", strlen("next")) == 0)
        {
            cmd = IPCWorkspaceSelectAdjacent;
            arg = 1;
        }
        else
        {
            if ((arg = atoi(argv[2])) > 0)
                cmd = IPCWorkspaceSelect;
        }
    }

    if (cmd != IPCLast)
    {
        if (!send_command(cmd, arg))
            exit(EXIT_FAILURE);
    }
    else
    {
        fprintf(stderr, __NAME_C__": Unknown command\n");
            exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
}
