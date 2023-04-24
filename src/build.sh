#!/bin/bash

# bear -- ./build.sh

D=$(dirname $(readlink -f $0))


clang++ -std=c++20 -O1 -fsanitize=address -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -Wextra -Weverything -Wno-c++98-compat  -Wno-c++98-compat-pedantic -Wno-unsafe-buffer-usage -Wno-missing-prototypes -Wno-covered-switch-default -Wno-switch-enum -Wno-unused-private-field -Wno-shorten-64-to-32 -Wno-old-style-cast -Wno-sign-conversion -g -isystem $HOME/cts/root/include -I ~/src/openh264/codec/api/wels/ $D/main.cpp -o $D/vd 
