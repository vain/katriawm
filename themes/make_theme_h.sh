#!/bin/bash

# Ugh, uhm, I guess this only works on GNU.
# And ImageMagick is required.

dir=$(readlink -e "$1")

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

# The following code converts any image readable by ImageMagick into an
# array of uint32_t. This is achieved by first converting it into a raw
# PPM file ("-compress losless"). In binary, this format is already
# pretty close to what we want. We convert it into hex using od. Bytes
# are grouped in packs of three, thus each group will represent one
# pixel. We prepend a "0x" and then finally neatly fold the array to 72
# characters in width.
echo 'static uint32_t dec_img[] = {'

bytes=$(identify -format '%[fx:w*h*3]' "$dir/$source_image")
convert -quiet -compress lossless -depth 8 "$dir/$source_image" ppm:- |
tail -c "$bytes" |
od -vt x1 -w3 -An |
sed 's/^/0x/; s/ //g' |
paste -sd, |
sed 's/,/, /g' |
fold -sw 72

echo '};'

echo
echo '#endif /* _WM_THEME_H */'
