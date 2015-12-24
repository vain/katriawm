#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include "ipc.h"

Atom
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

int
main(int argc, char **argv)
{
    Atom cwa;
    Display *dpy;
    Window root, command_window;
    XEvent ev;

    dpy = XOpenDisplay(NULL);
    root = DefaultRootWindow(dpy);

    cwa = (Window)getatomprop(
            dpy, root, "_"__NAME_UPPERCASE__"_COMMAND_WINDOW", XA_WINDOW
    );
    if (cwa == None)
    {
        fprintf(stderr, __NAME__"c: Cannot find command window\n");
        exit(EXIT_FAILURE);
    }
    command_window = (Window)cwa;

    memset(&ev, 0, sizeof ev);
    ev.xclient.type = ClientMessage;
    ev.xclient.window = root;
    ev.xclient.message_type = XInternAtom(
            dpy, "_"__NAME_UPPERCASE__"_CLIENT_COMMAND", False
    );

    ev.xclient.format = 8;
    ev.xclient.data.b[0] = IPCNoop;
    ev.xclient.data.b[1] = 42;

    XSendEvent(dpy, command_window, False, NoEventMask, &ev);
    XSync(dpy, False);

    exit(EXIT_SUCCESS);
}
