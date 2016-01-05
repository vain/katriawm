static struct DecorationGeometry dgeo = {
    .top_height = 20,
    .left_width = 1,
    .right_width = 1,
    .bottom_height = 5,
};

static struct SubImage dec_coords[] = {
    { .x = 0,  .y = 0, .w = 2, .h = 20 },  /* DecTopLeft */
    { .x = 2,  .y = 0, .w = 1, .h = 20 },  /* DecTop */
    { .x = 51, .y = 0, .w = 2, .h = 20 },  /* DecTopRight */

    { .x = 0,  .y = 0, .w = 1, .h = 1 },  /* DecLeft */
    { .x = 0,  .y = 0, .w = 1, .h = 1 },  /* DecRight */

    { .x = 0,  .y = 51, .w = 2, .h = 5 },  /* DecBottomLeft */
    { .x = 2,  .y = 51, .w = 1, .h = 5 },  /* DecBottom */
    { .x = 51, .y = 51, .w = 2, .h = 5 },  /* DecBottomRight */
};

static bool dec_has_title = true;
static struct TitleArea dec_title = {
    .left_offset = 5,
    .right_offset = 5,
    .baseline_top_offset = 14,
};
