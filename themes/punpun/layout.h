static struct DecorationGeometry dgeo = {
    .top_height = 40,
    .left_width = 10,
    .right_width = 10,
    .bottom_height = 10,
};

static struct SubImage dec_coords[] = {
    { .x = 0,  .y = 0, .w = 56, .h = 41 },  /* DecTopLeft */
    { .x = 56, .y = 0, .w = 1,  .h = 41 },  /* DecTop */
    { .x = 54, .y = 0, .w = 30, .h = 41 },  /* DecTopRight */

    { .x = 0,  .y = 41, .w = 10, .h = 1 },  /* DecLeft */
    { .x = 74, .y = 41, .w = 10, .h = 1 },  /* DecRight */

    { .x = 0,  .y = 39, .w = 10, .h = 10 },  /* DecBottomLeft */
    { .x = 10, .y = 30, .w = 1,  .h = 10 },  /* DecBottom */
    { .x = 74, .y = 39, .w = 10, .h = 10 },  /* DecBottomRight */
};

static bool dec_has_title = false;
static struct TitleArea dec_title = {
    .left_offset = 11,
    .right_offset = 11,
    .baseline_top_offset = 18,
};
