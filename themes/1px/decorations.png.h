static struct DecorationGeometry dgeo = {
    .top_height = 1,
    .left_width = 1,
    .right_width = 1,
    .bottom_height = 1,
};

static struct SubImage dec_coords[] = {
    { .x = 0, .y = 0, .w = 1, .h = 1 },  /* DecTopLeft */
    { .x = 0, .y = 0, .w = 1, .h = 1 },  /* DecTop */
    { .x = 0, .y = 0, .w = 1, .h = 1 },  /* DecTopRight */

    { .x = 0, .y = 0, .w = 1, .h = 1 },  /* DecLeft */
    { .x = 0, .y = 0, .w = 1, .h = 1 },  /* DecRight */

    { .x = 0, .y = 0, .w = 1, .h = 1 },  /* DecBottomLeft */
    { .x = 0, .y = 0, .w = 1, .h = 1 },  /* DecBottom */
    { .x = 0, .y = 0, .w = 1, .h = 1 },  /* DecBottomRight */
};

static bool dec_has_title = false;
static struct TitleArea dec_title = { 0 };
