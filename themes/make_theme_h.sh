#!/bin/sh

dir=$1

# Reads:
#  - $image
#  - $image_width
#  - $image_height
. "$dir"/metadata || exit 1

echo '#ifndef _WM_THEME_H'
echo '#define _WM_THEME_H'
echo
cat "$dir"/theme_base.h
echo

cat "$dir"/"$image".h
echo
echo "static unsigned int dec_img_w = $image_width;"
echo "static unsigned int dec_img_h = $image_height;"

echo 'static uint32_t dec_img[] = {'

# Our input file is expected to be a raw color PPM file with 8 bit
# depth per channel. We will now convert this into an uint32_t array.
# First, we create a hexdump using od. tr + grep will transform this
# into one byte per line. sed will group it in packs of three bytes and
# prepend a "0x". "s/[^a-zA-Z0-9]//g" is a simple way to remove newlines
# from the pattern buffer in a portable way. paste + fold will then
# transform this into a nicely formatted C array with comma separators.

tail -c `echo $image_width \* $image_height \* 3 | bc` "$dir"/"$image" |
od -vt x1 -An |
tr ' ' '\n' | grep -v '^$' |
sed 's/^/0x/; N; N; s/[^a-zA-Z0-9]//g' |
paste -sd, - | fold -sw 72

echo '};'

echo
echo '#endif /* _WM_THEME_H */'
