static struct DecorationGeometry dgeo = {
    .top_height = 23,
    .left_width = 6,
    .right_width = 6,
    .bottom_height = 6,
};

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

static char dec_has_title = 1;
static struct TitleArea dec_title = {
    .left_offset = 11,
    .right_offset = 11,
    .baseline_top_offset = 18,
};
