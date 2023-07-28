#!/bin/bash

# bear -- ./build.sh

D=$(dirname $(readlink -f $0))
SRC=$D/src

cc_args=(-std=c++2b)

debug=true
sanitize=false

if $debug ; then 
    cc_args+=( -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -g )
else
    cc_args+=( -O3 -DNDEBUG )
fi

$sanitize && cc_args+=( -fsanitize=address )

includes=( -isystem $HOME/cts/root/include -I $HOME/src/openh264/codec/api/wels/ )

g++ ${cc_args[@]} ${includes[@]} $SRC/main.cpp  -o $D/vd
