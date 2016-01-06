Using lemonbar with katriawm
============================

First of all, there is no need to use [lemonbar]. It’s just an example.
I think, though, that it works quite well and it is what I use myself.

In the repo, you can find `src/barinfo` which contains a tool to monitor
and evaluate the root window property `_KATRIA_STATE`. This property
contains some of katriawm’s internal state. See also
[`DOCUMENTATION.md`][katriadoc].

In fact, I do use two instances of lemonbar: One shows `_KATRIA_STATE`,
the other is a generic info bar. I’ll only talk about the first one
here.

Note that you can configure katriawm’s workarea offsets in your
`config.h` to make room for the bar.

[lemonbar]: https://github.com/LemonBoy/bar
[katriadoc]: DOCUMENTATION.md

Running
-------

I suggest you run it like this:

    while sleep 1; do katriabi; done |
    while sleep 1; do lemonbar -d ...; done

This way, you can put this in your `~/.xinitrc` and simply issue a
`killall katriabi` if you make changes to the binary.

Plus, lemonbar does not handle XRandR changes. This can be easily worked
around by restarting lemonbar (see [upstream issue][is]). Thus, after
doing `xrandr --output ...`, you have to do a `killall lemonbar`. A loop
like the one above ensures that lemonbar will be restarted automatically
and continues to read status information.

You will most likely want to use these default arguments for lemonbar:

-   `-d`: Make lemonbar use the `override_redirect` flag, so it docks
    properly.
-   `-g x20`: Set the bar’s height to 20 pixels.

[is]: https://github.com/LemonBoy/bar/issues/135

Configuration
-------------

katriabi is configured by means of a `config.h` file. A default file
`config.def.h` is provided. It should be pretty self-explanatory, the
main purpose is to set lemonbar’s color attributes.

`config.h` also defines the array `ws_names`:

    char *ws_names[WORKSPACE_MAX + 1] = {
        [1] = "www",
        [7] = "irc",
    };

This means that workspace `1` will be displayed as `1:www` and workspace
`7` as `7:irc`. All other workspaces are unaffected.
