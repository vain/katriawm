Theming
=======

Directories
-----------

Let’s say, the name of your theme will be `win311`. Thus, you’ll create
a new subdirectory under `themes` called `win311`. Everything will take
place in that directory.

Drafting and basic files
------------------------

You start a tool like GIMP and begin drafting the decorations. You
should start with an image of about 60x60 pixels. Just draw a tiny
little window with all black content. You can use RGB color.

You need to create three files:

-   `dec_img_normal.ff`: Decorations for the window when not being
    selected.
-   `dec_img_select.ff`: A selected (“focused”) window.
-   `dec_img_urgent.ff`: An unselected window with the urgency flag set.

Each file contains the full decorations, not only parts of it like “the
top left corner”. See below on how katriawm will interpret your image
files.

As `.ff` indicates, these files must use the [farbfeld] image format.
Although it’s unlikely to change any time soon, here’s a copy of the
format specification:

    FARBFELD IMAGE FORMAT SPECIFICATION

    +--------+---------------------------------------------------------+
    | Bytes  | Description                                             |
    +--------+---------------------------------------------------------+
    | 8      | "farbfeld" magic value                                  |
    +--------+---------------------------------------------------------+
    | 4      | 32-Bit BE unsigned integer (width)                      |
    +--------+---------------------------------------------------------+
    | 4      | 32-Bit BE unsigned integer (height)                     |
    +--------+---------------------------------------------------------+
    | [2222] | 4*16-Bit BE unsigned integers [RGBA] / pixel, row-major |
    +--------+---------------------------------------------------------+

If your image editing tool does not support saving farbfeld images, just
save them as a PNG and use the farbfeld tools to convert the files.

[farbfeld]: http://tools.suckless.org/farbfeld/

Colors and Fonts: `colorsfonts.h`
---------------------------------

For drawing the title bars, you need to define a font:

    static char *dec_fonts[] = {
        "Sans:pixelsize=10:style=bold:antialias=false",  /* FontTitle */
    };

As you can see, you must use Xft font descriptions.

Fonts have colors, too. One color for each tinting color. They’re
defined in another array:

    static char *dec_font_colors[] = {
        "#AFAFAF",  /* DecTintNormal */
        "#000000",  /* DecTintSelect */
        "#000000",  /* DecTintUrgent */
    };

Note that you can use Xft color names here.

Layout: `layout.h`
------------------

Window decorations consist of eight parts: Top left, top, top right,
left, right, bottom left, bottom, and bottom right.

    +----+--------+----+
    | tl |  top   | tr |
    +----+--------+----+
    | l  |        |  r |
    | e  |        |  i |
    | f  |        |  g |
    | t  |        |  h |
    |    |        |  t |
    +----+--------+----+
    | bl | bottom | br |
    +----+--------+----+

It’s important to understand how these are drawn. First, top, left,
right, and bottom are drawn – but without leaving space for the corners.
So, the intermediate image will look like this:

    +----+--------+----+
    |    |  top   |    |
    |    |--------|    |
    | l  |        |  r |
    | e  |        |  i |
    | f  |        |  g |
    | t  |        |  h |
    |    |        |  t |
    |    |--------|    |
    |    | bottom |    |
    +----+--------+----+

Then, the corners will be drawn on top of that.

You must tell katriawm about the dimensions of your decorations, which
should be pretty self-explanatory – all of the following happens in
`layout.h`:

    static struct DecorationGeometry dgeo = {
        .top_height = 23,
        .left_width = 6,
        .right_width = 6,
        .bottom_height = 6,
    };

Following that, you tell it which parts of your image are actually to be
used in the final window decoration:

    static struct SubImage dec_coords[] = {
        { .x = 0,  .y = 0, .w = 24, .h = 24 },  /* DecTopLeft */
        { .x = 29, .y = 0, .w = 1,  .h = 23 },  /* DecTop */
        { .x = 35, .y = 0, .w = 24, .h = 24 },  /* DecTopRight */

        { .x = 0,  .y = 29, .w = 6, .h = 1 },  /* DecLeft */
        { .x = 53, .y = 29, .w = 6, .h = 1 },  /* DecRight */

        { .x = 0,  .y = 35, .w = 24, .h = 24 },  /* DecBottomLeft */
        { .x = 29, .y = 53, .w = 1,  .h = 6  },  /* DecBottom */
        { .x = 35, .y = 35, .w = 24, .h = 24 },  /* DecBottomRight */
    };

Please note that each sub-image must have a width and height of at least
1 pixel. You can’t create “empty” sub-images.

So, for example, the bottom right sub-image begins at coordinates
(35, 35) in your original image which you have drawn in GIMP. The
sub-image shall have a height and width of 24 pixels.

Window decorations can have title areas. If they do, you must define the
following variables:

    static bool dec_has_title = true;
    static struct TitleArea dec_title = {
        .left_offset = 11,
        .right_offset = 11,
        .baseline_top_offset = 18,
    };

A font has a baseline. This is where most characters will “sit”.

Creating `theme.h`
------------------

Up to this point, you have created a theme. To actually use it, you have
to create a file called `theme.h` which must be located in the same
directory as `wm.c` itself. To do so, you should use the following
script:

    $ cd katriawm/themes
    $ ./make_theme_h.sh win311 >../src/core/theme.h

Where `win311` is the name of the directory you just created.

You can now recompile katriawm.
