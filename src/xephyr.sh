#!/bin/bash

make DEBUG_CPPFLAGS=-DDEBUG DEBUG_CFLAGS=-g || exit 1

Xephyr :80 &
sleep 1

export DISPLAY=:80
sxhkd &

while sleep 1; do core/katriawm; done
