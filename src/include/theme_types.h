#ifndef _WM_THEME_TYPES_H
#define _WM_THEME_TYPES_H

enum DecorationLocation
{
    DecTopLeft = 0,    DecTop = 1,    DecTopRight = 2,
    DecLeft = 3,                      DecRight = 4,
    DecBottomLeft = 5, DecBottom = 6, DecBottomRight = 7,

    DecLAST = 8,
};

enum DecorationWindowLocation
{
    DecWinTop = 0,
    DecWinLeft = 1,
    DecWinRight = 2,
    DecWinBottom = 3,

    DecWinLAST = 4,
};

enum DecState
{
    DecStateNormal = 0,
    DecStateSelect = 1,
    DecStateUrgent = 2,

    DecStateLAST = 3,
};

enum Font
{
    FontTitle = 0,

    FontLAST = 1,
};

struct DecorationGeometry
{
    int top_height;
    int left_width, right_width;
    int bottom_height;
};

struct SubImage
{
    int x, y, w, h;
};

struct TitleArea
{
    int left_offset;
    int right_offset;
    int baseline_top_offset;
};

#endif /* _WM_THEME_TYPES_H */
