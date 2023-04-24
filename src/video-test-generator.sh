#!/bin/bash

set -eu


W=176
H=144


# I followed by 9P
test1(){
ffmpeg -hide_banner -y -f lavfi -i testsrc=duration=1:size=${W}x${H}:rate=30 -pix_fmt yuv420p -f rawvideo output.yuv
x264 --input-res "${W}x${H}" --fps 30  --profile main --level 4.0 --preset slow --input-csp i420 \
     --keyint 10 --min-keyint 10 \
     --output output.h264 output.yuv
}

# I followed by 9P  interlaced
test2(){
#ffmpeg -hide_banner -y -f lavfi -i yuvtestsrc=duration=2:size=${W}x${H}:rate=30 -pix_fmt yuv420p -f rawvideo output.yuv
ffmpeg -y -f lavfi -i testsrc=duration=2:size=${W}x${H}:rate=30 -vf "interlace=tff" -pix_fmt yuv420p -f rawvideo output.yuv
#v4l2-ctl -d2 --set-fmt-video=width=$W,height=$H,pixelformat=YV12 --stream-mmap --stream-count=30 --stream-to=output.yuv
x264 --input-res "176x144" --fps 30 --tff --profile main --level 1.0 --preset slow --input-csp i420 \
     --keyint 10 --min-keyint 10 --no-scenecut --bframes 0 \
     --output clip-a-interlaced.h264 out.yuv
}
x264 ~/test.yuv --output interlacedfields.h264 --input-res 176x144 --fps 25 --preset medium --profile high --level 4.1 --bitrate 4000 --bframes 0 --keyint 10 --min-keyint 10 --scenecut 0 --rc-lookahead 50 --interlaced --tff

t(){
test1
test2
}

df(){
in=${1?"First arg: input filename"}
field=${2:-".*"}
ffprobe -hide_banner -select_streams v:0 -show_frames  $in | grep -E "$field"  | perl -ne ' print $i++. "  $_ " ;'
}

pict_types(){
in=${1?"First arg: input filename"}
ffprobe -hide_banner -select_streams v:0 -show_frames  $in  | grep pict_type | sed 's/pict_type=//g' | xargs
}


eval $*
