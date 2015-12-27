static struct DecorationGeometry dgeo = {
    .top_height = 27,
    .left_width = 7,
    .right_width = 7,
    .bottom_height = 7,
};

static unsigned int dec_tints[] = {
    0x6a3f3f,  /* DecTintNormal */
    0xffcb38,  /* DecTintSelect */
    0xff0000,  /* DecTintUrgent */
};

static struct SubImage dec_coords[] = {
    { .x = 0,  .y = 0, .w = 28, .h = 28 },  /* DecTopLeft */
    { .x = 29, .y = 0, .w = 1,  .h = 27 },  /* DecTop */
    { .x = 31, .y = 0, .w = 28, .h = 28 },  /* DecTopRight */

    { .x = 0,  .y = 29, .w = 7, .h = 1 },  /* DecLeft */
    { .x = 52, .y = 29, .w = 7, .h = 1 },  /* DecRight */

    { .x = 0,  .y = 31, .w = 28, .h = 28 },  /* DecBottomLeft */
    { .x = 29, .y = 52, .w = 1,  .h = 7  },  /* DecBottom */
    { .x = 31, .y = 31, .w = 28, .h = 28 },  /* DecBottomRight */
};

static char dec_has_title = 1;
static struct TitleArea dec_title = {
    .left_offset = 13,
    .right_offset = 13,
    .baseline_top_offset = 21,
};

static char *dec_fonts[] = {
    "Sans:pixelsize=12:style=bold:antialias=false",  /* FontTitle */
};

static char *dec_font_colors[] = {
    "#AFAFAF",  /* DecTintNormal */
    "#000000",  /* DecTintSelect */
    "#000000",  /* DecTintUrgent */
};
