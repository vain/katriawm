#!/bin/bash

# These variables depend on __NAME__, ipc.h, and WORKSPACE_MAX.
atom_name=_KATRIA_STATE
layout_names[0]='[]='
layout_names[1]='[M]'
layout_names[2]='><>'
size_monws=16

style_nor='%{B-}%{F-}%{-u}%{-o}'
style_sel='%{+u}%{+o}'
style_urg='%{R}'

while getopts n:s:u: name
do
    case $name in
        n) style_nor=$OPTARG ;;
        s) style_sel=$OPTARG ;;
        u) style_urg=$OPTARG ;;
    esac
done

# This file can contain an array of suffixes. For example,
# `ws_names[4]='dev'` will make workspace 4 appear as '4:dev'.
declare -A ws_names
[[ -r ~/.katriabar ]] && . ~/.katriabar

# "$atom_name" is, of course, a non-standard atom. Thus, it's not
# allocated before katriawm has started. This makes xprop fail instead
# of waiting until the atom is present. So, it's our job to wait.
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
sed -u 's/^[^=]\+= //; s/,//g; /[^- 0-9]/d' |
while read line
do
    info=($line)
    mn=${info[0]}
    smon=${info[1]}
    slots=${info[2]}
    (( smon < 0 )) && (( smon += 256 ))
    (( slots < 0 )) && (( slots += 256 ))

    out=
    for (( i = 0; i < mn; i++ ))
    do
        out+="%{S$i}%{l} "
        out+="${layout_names[${info[3 + i + mn]}]} "

        active_workspace=${info[3 + i]}

        offset_ws=$((3 + mn + mn + i * size_monws))
        ws_num=0
        for (( byte_i = 0; byte_i < size_monws; byte_i++ ))
        do
            byte=${info[offset_ws + byte_i]}
            ubyte=${info[offset_ws + byte_i + mn * size_monws]}
            (( byte < 0 )) && (( byte += 256 ))
            (( ubyte < 0 )) && (( ubyte += 256 ))
            mask=1
            for (( bit = 0; bit < 8; bit++ ))
            do
                if [[ -z ${ws_names[$ws_num]} ]]
                then
                    show=$ws_num
                else
                    show="$ws_num:${ws_names[$ws_num]}"
                fi
                if (( ubyte & mask ))
                then
                    out+=$style_urg
                    out+=" $show "
                    out+=$style_nor
                elif (( byte & mask )) || (( ws_num == active_workspace ))
                then
                    if (( ws_num == active_workspace ))
                    then
                        out+=$style_sel
                        out+=" $show "
                        out+=$style_nor
                    else
                        out+=" $show "
                    fi
                fi
                (( ws_num++ ))
                (( mask <<= 1 ))
            done
        done

        out+='%{c}'
        (( i == smon )) && out+="${style_sel} SELECTED ${style_nor}"

        out+='%{r}'
        mask=1
        for (( bit = 0; bit < 8; bit++ ))
        do
            if (( slots & mask ))
            then
                out+="$bit"
            fi
            (( mask <<= 1 ))
        done
        out+=' '
    done
    echo "$out"
done
