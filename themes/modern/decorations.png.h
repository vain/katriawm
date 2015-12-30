static struct DecorationGeometry dgeo = {
    .top_height = 23,
    .left_width = 5,
    .right_width = 5,
    .bottom_height = 5,
};

static struct SubImage dec_coords[] = {
    { .x = 0,  .y = 0, .w = 23, .h = 23 },  /* DecTopLeft */
    { .x = 25, .y = 0, .w = 1,  .h = 23 },  /* DecTop */
    { .x = 36, .y = 0, .w = 23, .h = 23 },  /* DecTopRight */

    { .x = 0,  .y = 30, .w = 5, .h = 1 },  /* DecLeft */
    { .x = 54, .y = 30, .w = 5, .h = 1 },  /* DecRight */

    { .x = 0,  .y = 36, .w = 23, .h = 23 },  /* DecBottomLeft */
    { .x = 25, .y = 54, .w = 1,  .h = 5  },  /* DecBottom */
    { .x = 36, .y = 36, .w = 23, .h = 23 },  /* DecBottomRight */
};

static char dec_has_title = 1;
static struct TitleArea dec_title = {
    .left_offset = 9,
    .right_offset = 9,
    .baseline_top_offset = 16,
};
