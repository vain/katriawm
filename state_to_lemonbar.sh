#!/bin/bash

# TODO evaluate urgency hints once they're published

# These variables depend on __NAME__, ipc.h, and WORKSPACE_MAX.
atom_name=_KATRIA_STATE
layout_names[0]='[]='
layout_names[1]='[M]'
layout_names[2]='><>'
size_monws=16

for (( i = 0; i < 100; i++ ))
do
    if (( i == 99 ))
    then
        echo "Atom '$atom_name' not found on root window" >&2
        exit 1
    fi

    if xprop -root "$atom_name" 2>&1 | grep -vq 'no such atom'
    then
        break
    fi

    sleep .1
done

xprop -spy -notype -root "$atom_name" |
sed -u 's/^[^=]\+= //; s/,//g' |
while read line
do
    info=($line)
    mn=${info[0]}

    out=
    for (( i = 0; i < mn; i++ ))
    do
        out+="%{S$i} "
        out+="${layout_names[${info[1 + i + mn]}]} "

        active_workspace=${info[1 + i]}

        offset_ws=$((1 + mn + mn + i * size_monws))
        ws_num=1
        for (( byte_i = 0; byte_i < size_monws; byte_i++ ))
        do
            byte=${info[offset_ws + byte_i]}
            mask=1
            for (( bit = 0; bit < 8; bit++ ))
            do
                if (( byte & mask )) || (( ws_num == active_workspace ))
                then
                    if (( ws_num == active_workspace ))
                    then
                        out+="%{R}"
                        out+=" $ws_num "
                        out+="%{R}"
                    else
                        out+=" $ws_num "
                    fi
                fi
                ((ws_num++))
                ((mask <<= 1))
            done
        done
    done
    echo "$out"
done
