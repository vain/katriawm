Setting up katriawm
===================

This document walks you through a basic setup of katriawm. It does not
cover advanced topics, but is meant as a starting point.


Preparing your X11 session
--------------------------

To avoid having to deal with login managers, you should start with a
very simple setup: Configure your operating system to boot to a text
console. When you login on said console, you should be presented with a
shell.

Install an X11 server, but no desktop environment or other tools. On
Arch Linux, you can do that by installing the package `xorg-server`. A
convenient way to actually start X11 is xinit which you can install from
the package `xorg-xinit`. If necessary, take care of configuring the X11
server, but most things should work out of the box these days. Please
consult the documentation of your operating system, if needed. You will
also most likely want to have a terminal emulator as well, so install
the package `xterm`.

When you run `startx`, a basic X11 setup should boot up.


Preparing your build environment
--------------------------------

Make sure you have a C compiler ready. Most distributions provide a meta
package that pulls in all required packages. On Arch Linux, this is
`base-devel`. Install that package and we’re good to go.

Also install `git` if necessary.

On Arch Linux, installing `xorg-server` as mentioned above already pulls
in the two library packages `libx11` and `libxft`. You need both. You
also need `libxrandr`.


Getting and compiling katriawm
------------------------------

As katriawm is configured by editing `config.h`, a precompiled package
by your distribution doesn’t make a lot of sense. Instead, we’re going
to compile from source code.

I suggest you put all the code in one directory. Let’s assume that path
is `~/git`.

Let’s get katriawm:

    $ cd ~/git
    $ git clone https://github.com/vain/katriawm

We’re going to skip customization for now. You can edit `config.h` files
later (they don’t exist yet, but they are copied from `config.def.h`
files), if you want to. We’re also going to skip theming.

Compile a vanilla version of katriawm:

    $ cd katriawm/src
    $ make

The output should look like this:

    $ make
    cp core/config.def.h core/config.h
    ../themes/make_theme_h.sh ../themes/motif >core/theme.h
    cp barinfo/config.def.h barinfo/config.h
    core/wm.o
    util/util.o
    core/katriawm
    core/client.o
    core/katriac
    barinfo/barinfo.o
    barinfo/katriabi

If it does, you have successfully compiled katriawm.


Installing a hotkey daemon
--------------------------

katriawm is a window manager – not a bar, nor a hotkey daemon. You must
use additional tools for these jobs. We’ll cover the bar later. First,
let’s install a hotkey daemon.

I suggest using [sxhkd], version 0.5.5. Later versions lack features
that are very important for katriawm. Get the code:

    $ cd ~/git
    $ git clone https://github.com/baskerville/sxhkd

Go back to version 0.5.5:

    $ cd sxhkd
    $ git checkout 0.5.5

To compile sxhkd, you need additional libraries. On Arch Linux, you have
to install the packages `xcb-util` and `xcb-util-keysyms`. Once they are
available, compile sxhkd:

    $ make

[sxhkd]: https://github.com/baskerville/sxhkd


Make the tools available in your `$PATH`
----------------------------------------

It’s up to you how to do this. The binaries `katriawm`, `katriac`,
`katriabi`, and `sxhkd` should be available in your `$PATH`.

The easiest way is to edit a file which is run when you log in on your
console. If you’re using GNU Bash, you will probably want to edit
`~/.bashrc`. Add lines like this at the end:

    PATH=$PATH:~/git/katriawm/src/core
    PATH=$PATH:~/git/katriawm/src/barinfo
    PATH=$PATH:~/git/sxhkd
    export PATH

You can, of course, put all that stuff into one line.


A simple config file for sxhkd
------------------------------

katriawm itself is only the window manager. It provides a simple IPC
mechanism, meaning you can talk to it using another program. This
program is katriac, the client. To move windows around, to resize
windows, to switch focus, and so on, you invoke katriac which, in turn,
talks to katriawm.

katriawm is meant to be controlled mainly by keyboard hotkeys. That’s
why we’re using a hotkey daemon. There are no menus in katriawm.

This means that we have to write a config file for sxhkd. Save it as
`~/.config/sxhkd/sxhkdrc`. Here is a very short example:

    super + Return
        xterm

    super + {j,k,Tab}
        katriac client_select {next,prev,recent}

    super + shift + BackSpace
        katriac wm_quit

Now you have a hotkey to launch a new window, some hotkeys to change the
current window, and a hotkey to quit katriawm.

There are many more commands available. I suggest you have a look at my
config file:

-   <https://github.com/vain/dotfiles-pub/blob/master/sxhkdrc>


A very basic X11 session with katriawm and sxhkd
------------------------------------------------

Now that you have the most essential tools available, you can start to
build your X11 session. Do so by editing the file `~/.xinitrc`:

    #!/bin/sh

    sxhkd &
    katriawm

This file is a shell script, so make it executable.

What’s happening here? Not much, we’re starting sxhkd in the background
and then we run katriawm.

Now this is the point where you can log into your text console and run
`startx`. The X11 server should start. You should see a mouse cursor and
a black screen or the typical X11 background pattern. Nothing more.

If you now hit `Super + Return`, a new terminal window should appear. If
you hit that hotkey again, another terminal should appear. katriawm
should be in tiling mode, thus automatically arranging the windows for
you.

Congratulations! This is a basic katriawm setup.

You can now hit `Super + Shift + BackSpace` to quit katriawm. You should
end up back on your text console.



Setting up a bar for katriawm
=============================

You will very likely want to use a bar. It shows important information,
such as the current workspace you’re on.


Using bevelbar
--------------

I wrote [bevelbar] shortly after I wrote katriawm. It comes with a
traditional pseudo-3D bevel look that works very well together with
katriawm.

[bevelbar]: https://github.com/vain/bevelbar

Get the code:

    $ cd ~/git
    $ git clone https://github.com/vain/bevelbar

You should already have all the required dependencies. Thus, you can
just compile it:

    $ cd bevelbar
    $ make

You will probably want to make the binary available in your `$PATH` as
well, so edit your `~/.bashrc` once more:

    PATH=$PATH:~/git/bevelbar

Now, you have to instruct your X11 session to run the bar. The bar
*itself* doesn’t do anything – you must feed it some input, i.e. text to
display. katriawm comes with a tool `katriabi` that can do this for you.
It reads the internal state of katriawm and displays it in bevelbar.

Edit your `~/.xinitrc` and insert this above the line where you start
katriawm:

    (
        while sleep 1; do katriabi bevelbar; done |
        while sleep 1; do bevelbar; done
    ) &

The reasoning behind these `while` loops is explained in
[`BARS.md`](BARS.md).

If you now run `startx` again, you will see a little bar at the bottom
of the screen.



... and beyond
==============

If you got this far, you have a simple setup of katriawm, sxhkd, and
bevelbar.

Now is the time to dive deeper into configuring katriawm. Learn how to
set up and use workspaces. Learn how to set up multi-monitoring. The man
pages and the other files in the `doc` directory will tell you how to do
that. Maybe you will even want to learn about [theming] katriawm or
about its [internals].

[theming]: THEMING.md
[internals]: INTERNALS.md

Maybe you’re also interested in these two projects:

-   <https://github.com/vain/infofeld>: Generate system information
    graphics (cpu load, memory usage, network activity, and the like) as
    farbfeld images which can be shown in bevelbar.
-   <https://github.com/vain/xpointerbarrier>: Create pointer barriers
    around each physical monitor. Very helpful if you have more than one
    monitor.
