Using bars with katriawm
========================

In the repo, you can find `src/barinfo` which contains a tool to monitor
and evaluate the root window property `_KATRIA_STATE`. This property
contains some of katriawm’s internal state. See also
[`DOCUMENTATION.md`][katriadoc].

I, myself, use [bevelbar]. This tool was written after katriawm, so the
barinfo tool still contains code to generate output suitable for
[lemonbar].

In fact, I do use two instances of bevelbar: One shows `_KATRIA_STATE`,
the other is a generic info bar. I’ll only talk about the first one
here.

Note that you can configure katriawm’s workarea offsets in your
`config.h` to make room for the bar.

Please also note that there is no need to use either [bevelbar] or
[lemonbar]. These are just examples. However, bevelbar’s visual style
fits best with katriawm’s default theme.

[bevelbar]: https://github.com/vain/bevelbar
[lemonbar]: https://github.com/LemonBoy/bar
[katriadoc]: DOCUMENTATION.md

Running
-------

I suggest you run it like this:

    while sleep 1; do katriabi; done |
    while sleep 1; do bevelbar ...; done

This way, you can put this in your `~/.xinitrc` and simply issue a
`killall katriabi` if you make changes to the binary.

Plus, bevelbar does not handle XRandR changes (nor does lemonbar). This
can be easily worked around by restarting the bar (see also lemonbar’s
[upstream issue][is]). Thus, after doing `xrandr --output ...`, you have
to do a `killall bevelbar` or `killall lemonbar`. A loop like the one
above ensures that the bar will be restarted automatically and continues
to read status information.

bevelbar requires many command line arguments. Please see its
documentation.

For lemonbar, you have to use the `-d` flag to make lemonbar use the
`override_redirect` flag on its windows, so it docks properly.

[is]: https://github.com/LemonBoy/bar/issues/135

Configuration
-------------

katriabi is configured by means of a `config.h` file. A default file
`config.def.h` is provided. It should be pretty self-explanatory, the
main purpose is to define “symbols” for layout algorithms and optional
names for workspaces:

    char *layout_names[LALast] = {
        [LATile] = "[]=",
        ...
    };

    char *ws_names[WORKSPACE_MAX + 1] = {
        [1] = "www",
        [7] = "irc",
        ...
    };

This means that workspace `1` will be displayed as `1:www` and workspace
`7` as `7:irc`. All other workspaces are unaffected.

`config.h` also defines attribute strings for lemonbar.
