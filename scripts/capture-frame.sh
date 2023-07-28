#!/bin/bash

W=320
H=240
# See: v4l2-ctl -d2 --list-formats-ext
FORMAT=NV12
FPS=30
# modprobe vivid; v4l2-ctl --list-devices (find vivid device, for example)
DEVICE=2

RAW=./test_1_$Wx$H_$FORMAT
H264_OUTPUT=./test_1_$Wx$H_nv12.h264

# Capture

# stream-count=2 is the minimum openh264 can work with (don't understand that)
v4l2-ctl -d2 --set-fmt-video=width=$W,height=$H,pixelformat=$FORMAT --stream-mmap --stream-count=3 --stream-to=test1.nv12 --stream-out-pattern=5 --stream-out-square

# Encode

x264 --input-res "${W}x${H}" --fps 60 --bitrate 500  --profile baseline --level 1 --input-csp NV12 --output test1.h264 test1.nv12

echo "$H264_OUTPUT"
