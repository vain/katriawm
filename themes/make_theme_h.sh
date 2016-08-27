#!/bin/sh

dir=$1

echo '#ifndef _WM_THEME_H'
echo '#define _WM_THEME_H'
echo
cat "$dir"/colorsfonts.h
echo

cat "$dir"/layout.h
echo

for i in 'normal' 'select' 'urgent'
do
    echo 'static uint8_t dec_img_'$i'[] = {'

    # Our input file is expected to be a farbfeld image file. We will
    # now convert this into a uint8_t array.
    #
    # (We could use "xxd -i" here but that would add a dependency on
    # Vim.)

    od -vt x1 -An <"$dir"/dec_img_$i.ff |
    tr ' ' '\n' |
    grep -v '^$' |
    sed 's/^/0x/' |
    paste -sd, - |
    fold -sw 70

    echo '};'
    echo
done

echo '#endif /* _WM_THEME_H */'
