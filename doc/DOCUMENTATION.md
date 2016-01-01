Design Principles
=================

-   Manage windows. Don’t try to be a hotkey daemon or to draw bars.
-   Don’t read any files at runtime. Everything is configured at compile time.
-   Provide reasonable floating and tiling behaviour.
-   Try to make clients at least 90% happy.


Monitors and Workspaces
=======================

We use XRandR to detect the currently detected monitors. The internal
list of monitors is always sorted by their offset in `x` direction
because that’s the most common way of setting up multiple monitors. The
monitor on the left gets index 0, the one right next to it gets index 1,
and so on.

Each monitor has its own set of workspaces. This means, when you change
the active workspace, it only changes on the current monitor. A
graphical representation of this concept would look like this:

    +--- MONITOR 0 ---+   +--- MONITOR 1 ---+   +--- MONITOR 2 ---+
    |                 |   |                 |   |                 |
    |  +-- WS  1 --+  |   |  +-- WS  7 --+  |   |  +-- WS 21 --+  |
    |  |           |  |   |  |           |  |   |  |           |  |
    |  |           |  |   |  |           |  |   |  |           |  |
    |  |           |  |   |  |           |  |   |  |           |  |
    |  +-----------+  |   |  +-----------+  |   |  +-----------+  |
    |                 |   |                 |   |                 |
    +-----------------+   +-----------------+   +-----------------+

This is in contrast to many other window managers. Most of them have
virtual desktops which span multiple screens. This means changing the
active desktop affects windows on *all* monitors. Visualization:

    +------------------------- DESKTOP 8 -------------------------+
    |                                                             |
    |  +-- MONITOR 0 --+   +-- MONITOR 1 --+   +-- MONITOR 2 --+  |
    |  |               |   |               |   |               |  |
    |  |               |   |               |   |               |  |
    |  |               |   |               |   |               |  |
    |  +---------------+   +---------------+   +---------------+  |
    |                                                             |
    +-------------------------------------------------------------+

The reason for that other model is the EWMH specification. [It
states][ewmh] that only one virtual desktop can be visible at a time.

This means katriawm breaks with EWMH. Why? Because I think the EWMH
model is really impractical. Plus, if you really want to, you can
instruct katriawm to *behave* like the EWMH model does, so it’s kind of
a superset.

[ewmh]: http://standards.freedesktop.org/wm-spec/wm-spec-latest.html#idm140200477421552

XRandR support and Save Slots
-----------------------------

XRandR is the predominant way of configuring your monitors these days.
Thus, katriawm supports XRandR and nothing else.

When changes to the monitor layout occur (new monitor activated, old
ones removed, monitors moved around, ...), katriawm moves all windows to
the very first monitor it can find. It does in no way try to preserve
your current layout.

However, you can manually save the current layout at any given time. You
can use up to ten save slots. Later on, you can of course restore a
layout. Exemplary workflow:

-   You connect your laptop to some external screens and work like that
    for some hours.
-   It’s time for a meeting.
-   You save the current layout, disconnect your laptop from the
    external screens, attend the meeting.
-   When you return, you reconnect the laptop to your monitors. Finally,
    you can restore your previous layout.


Client List and Focus List
==========================

Internally, katriawm stores each client in two lists. The first one is
very simple as it only stores all clients/windows managed by katriawm.
This is called the client list. Order in this list matters only for some
algorithms, like the tiling layout.

The second list is called the focus list. Again, every client is present
in this list. However, when a client is focused, it is moved to the head
of the list. Thus, we get a history of which clients have been focused.

Note that the focus list is just one list of all clients, i.e. it
contains clients from all workspaces and all monitors. So, when a window
is closed, how do we find the next client to focus? We must traverse
the focus list and search for the first client whose `workspace`
variable is set to the active workspace on the monitor. In other words,
we filter the list.

The focus list is a nice way to store the complete focus history for all
workspaces and all monitors. Thus, when you switch workspace and/or
monitor, your focus history on that workspace/monitor is still intact.


Decorations
===========

katriawm is a non-reparenting window manager. Still, it draws window
decorations. It does so by drawing to extra windows which are simply
moved right next to the window.

The window decorations are read from `theme.h` and compiled into the
final binary. The format of `dec_img[]` is simply an array of unsigned
32 bit integers, one for each pixel. That integer will be interpreted as
RGB. For example, the value `0x11223344` will be interpreted as:

-   Red channel: `22`
-   Green channel: `33`
-   Blue channel: `44`
-   The leading `11` will be ignored. We don’t support an alpha channel.

You can use colors, but `dec_img[]` is expected to be a grayscale image,
i.e. red = green = blue. The actual tints for normal, selected, and
urgent windows are specified in `dec_tints[]`. The resulting tinted
images will be created on the fly at runtime.

Yes, this is large. We could have used just one byte per pixel. It’s a
trade-off, you know. By storing 32 bits, we can save the code to convert
the image from 8 bits to 32 bits.

See `decorations_load()` and `decorations_draw_for_client()` for further
details.


IPC
===

User Commands
-------------

katriawm does not interpret user input such as keyboard or mouse events.
Instead, it listens for X11 ClientMessages on the root window. See
`send_command()` in `client.c` on how to construct such a message.

This approach has several advantages:

-   We don’t need to set up a UNIX socket or FIFO or anything else. The
    X server itself acts as an IPC mechanism and we are already
    connected to it. This saves us a lot of code and program logic.
-   If you happen to run multiple X servers (or Xephyr), you can easily
    run multiple instances of katriawm and katriac.
-   The X server also does authorization for us.
-   The client doesn’t need to be written in C. It currently is, though,
    because it’s rather simple.
-   Obviously, we don’t need to be a hotkey daemon. This again saves us
    a lot of program logic.

Publishing the Internal State for Bars
--------------------------------------

katriawm does not draw any bars, either. Thus, we rely on external
programs to do this.

Unfortunately, we break with the EWMH expectations, so you can’t use any
of the existing pagers out there. We show more than one virtual desktop
at a time. Or in other words: When changing the active workspace in
katriawm, this only happens on *one* monitor, not on all monitors
simultaneously. This breaks EWMH. EWMH thinks of one large desktop that
spans all screens, while we then of each screen having their own set of
virtual desktops.

katriawm introduces a new property on the root window called
`_KATRIA_STATE`. It’s a list of integers, following this format:

-   Index 0 is the number of monitors recognized by katriawm. Let’s call
    this `N`.
-   The next `N` bytes indicate the active workspace on each monitor. A
    workspace is an integer `w` where `1 <= w <= 127`.
-   The next `N` bytes indicate the visible layout algorithm on each
    monitor. See `ipc.h` to find out about the meaning of these
    integers.
-   What’s following next is a bitmask of occupied workspaces on the
    first monitor. This bitmask is 16 bytes long and ordered in little
    endian. For example, if the second byte is `0b00001010`, then
    workspaces number 10 and 12 are occupied.
-   After those 16 bytes, another 16 bytes follow. They are a bitmask
    just like the previous 16 bytes. If a bit is set, then there’s a
    client on that workspace with the urgency hint set.
-   The previous 32 bytes belong to monitor number 0. If there is more
    than one monitor connected to the computer, then there will be 32
    bytes for each monitor.

This property can be read by external tools in order to draw a pager or
a bar. This can be done using a shell script and the `xprop` utility.

As an example, there is a script that reads this property and produces
output suitable to be fed into [lemonbar].

[lemonbar]: https://github.com/LemonBoy/bar
