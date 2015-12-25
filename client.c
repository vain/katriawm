#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include "ipc.h"

static Atom
getatomprop(Display *dpy, Window w, char *name, Atom type) {
    /* Props to dwm */

    int di;
    unsigned long dl;
    unsigned char *p = NULL;
    Atom da, atom = None, prop;

    prop = XInternAtom(dpy, name, False);

    if (XGetWindowProperty(dpy, w, prop, 0L, sizeof atom, False, type,
                           &da, &di, &dl, &dl, &p) == Success && p)
    {
        atom = *(Atom *)p;
        XFree(p);
    }
    return atom;
}

static int
send_command(enum IPCCommand cmd, char arg)
{
    Atom cwa;
    Display *dpy;
    Window root, command_window;
    XEvent ev;

    if (!(dpy = XOpenDisplay(NULL)))
        return 0;

    root = DefaultRootWindow(dpy);

    cwa = (Window)getatomprop(dpy, root, IPC_ATOM_WINDOW, XA_WINDOW);
    if (cwa == None)
    {
        fprintf(stderr, __NAME_C__": Cannot find command window\n");
        return 0;
    }
    command_window = (Window)cwa;

    memset(&ev, 0, sizeof ev);
    ev.xclient.type = ClientMessage;
    ev.xclient.window = root;
    ev.xclient.message_type = XInternAtom(dpy, IPC_ATOM_COMMAND, False);

    ev.xclient.format = 8;
    ev.xclient.data.b[0] = cmd;
    ev.xclient.data.b[1] = arg;

    fprintf(stderr, __NAME_C__": Sending cmd %d, arg %d\n", cmd, arg);
    XSendEvent(dpy, command_window, False, NoEventMask, &ev);
    XSync(dpy, False);

    return 1;
}

int
main(int argc, char **argv)
{
    enum IPCCommand cmd = IPCLast;
    char arg;

    if (argc < 2)
    {
        fprintf(stderr, "Expected arguments\n");
        exit(EXIT_FAILURE);
    }

    if (strncmp(argv[1], "noop", strlen("noop")) == 0)
    {
        cmd = IPCNoop;
        arg = 42;
    }
    if (strncmp(argv[1], "mouse_move", strlen("mouse_move")) == 0 && argc >= 3)
    {
        if (strncmp(argv[2], "down", strlen("down")) == 0)
        {
            cmd = IPCMouseMove;
            arg = 0;
        }
        if (strncmp(argv[2], "motion", strlen("motion")) == 0)
        {
            cmd = IPCMouseMove;
            arg = 1;
        }
        if (strncmp(argv[2], "up", strlen("up")) == 0)
        {
            cmd = IPCMouseMove;
            arg = 2;
        }
    }
    if (strncmp(argv[1], "mouse_resize", strlen("mouse_resize")) == 0 && argc >= 3)
    {
        if (strncmp(argv[2], "down", strlen("down")) == 0)
        {
            cmd = IPCMouseResize;
            arg = 0;
        }
        if (strncmp(argv[2], "motion", strlen("motion")) == 0)
        {
            cmd = IPCMouseResize;
            arg = 1;
        }
        if (strncmp(argv[2], "up", strlen("up")) == 0)
        {
            cmd = IPCMouseResize;
            arg = 2;
        }
    }
    if (strncmp(argv[1], "nav_monitor", strlen("nav_monitor")) == 0 && argc >= 3)
    {
        if (strncmp(argv[2], "left", strlen("left")) == 0)
        {
            cmd = IPCNavMonitor;
            arg = -1;
        }
        if (strncmp(argv[2], "right", strlen("right")) == 0)
        {
            cmd = IPCNavMonitor;
            arg = 1;
        }
    }
    if (strncmp(argv[1], "nav_workspace", strlen("nav_workspace")) == 0 && argc >= 3)
    {
        if (strncmp(argv[2], "prev", strlen("prev")) == 0)
        {
            cmd = IPCNavWorkspaceAdj;
            arg = -1;
        }
        else if (strncmp(argv[2], "next", strlen("next")) == 0)
        {
            cmd = IPCNavWorkspaceAdj;
            arg = 1;
        }
        else
        {
            if ((arg = atoi(argv[2])) > 0)
                cmd = IPCNavWorkspace;
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
