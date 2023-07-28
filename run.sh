#!/bin/bash

D=$(dirname $(readlink -f $0))
SRC=$D/src

env LD_LIBRARY_PATH="$HOME/cts/root/lib:$HOME/cts/root/lib/x86_64-linux-gnu" \
    VK_LAYER_PATH="$HOME/cts/root/share/vulkan/explicit_layer.d:$HOME/cts/root/etc/vulkan/explicit_layer.d" \
    $D/vd $@
