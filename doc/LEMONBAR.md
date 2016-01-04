Using lemonbar with katriawm
============================

First of all, there is no need to use [lemonbar]. It’s just an example.
I think, though, that it works quite well and it is what I use myself.

The repo contains the script `state_to_lemonbar.sh`. It demonstrates how
you can monitor the root window property `_KATRIA_STATE` from a shell
script, process the bit masks, and feed the input to lemonbar.

In fact, I do use two instances of lemonbar: One shows `_KATRIA_STATE`,
the other is a generic info bar. I’ll only talk about the first one
here.

Note that you can configure katriawm’s workarea offsets in your
`config.h` to make room for the bar.

[lemonbar]: https://github.com/LemonBoy/bar

Running
-------

I suggest you run it like this:

    state_to_lemonbar.sh |
    while sleep 1; do lemonbar -d ...; done

The thing is, lemonbar does not handle XRandR changes. This can be
easily worked around by restarting lemonbar (see [upstream issue][is]).
Thus, after doing `xrandr --output ...`, you have to do a `killall
lemonbar`. A loop like the one above ensures that lemonbar will be
restarted automatically and continues to read status information.

[is]: https://github.com/LemonBoy/bar/issues/135

Configuration
-------------

### Command line arguments

The script accepts three parameters:

-   `-n`: Attribute string to prepend to unselected areas.
-   `-s`: The same for selected areas.
-   `-u`: The same for urgent areas.

Let’s say you want to change the background color for selected items,
then you have to call it like this:

    state_to_lemonbar.sh -s '%{B#554433}' ...

### Config file

There is a tiny, optional config file. It’s called `~/.katriabar`. It’s
a GNU Bash snippet which defines suffixes for workspace indexes. It
looks like this:

    ws_names[1]='www'
    ws_names[12]='irc'

The bar will then show workspace 1 as `1:www` and workspace 12 as
`12:irc`. All other workspaces are unaffected.

Dependencies
------------

The script requires:

-   `xprop`
-   GNU Bash
-   Probably GNU grep and GNU sed, not tested with other tools.


A word of warning
=================

As I said, this is only an example. And all this is kind of in a state
of flux. GNU tools are usually available on Linux, but I would love to
see everything run on BSD as well.
