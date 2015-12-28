#!/bin/bash

# Ugh, uhm, I guess this only works on GNU.
# And ImageMagick is required.

dir=$1

if [[ -z "$dir" ]]
then
    echo "Usage: $0 <directory>" >&2
    exit 1
fi

# Reads: $source_image
. "$dir"/metadata || exit 1

echo '#ifndef _WM_THEME_H'
echo '#define _WM_THEME_H'
echo
cat "$dir"/theme_base.h
echo

source_image_h="$dir"/"$source_image".h

size=$(identify "$dir/$source_image" | awk '{ print $3 }')
size_w=${size%x*}
size_h=${size#*x}

cat "$source_image_h"
echo
echo "static unsigned int dec_img_w = $size_w;"
echo "static unsigned int dec_img_h = $size_h;"
echo 'static unsigned int dec_img[] = {'

convert -quiet -compress lossless -depth 8 "$dir/$source_image" ppm:- |
sed '1,3d' |
od -vt x1 -w3 -An |
sed 's/^/0xff/' |
sed 's/ //g' |
paste -sd, |
sed 's/,/, /g' |
fold -sw 72

echo '};'

echo
echo '#endif /* _WM_THEME_H */'
