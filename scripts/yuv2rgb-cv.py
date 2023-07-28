import cv2
import numpy as np
import os
import subprocess as sp
import sys
import hashlib

QCIF="176x144"
QVGA="352x288"
FourK= "3840x2160"
WQXGA="2560x1600"

def width_height(desc):
    width, height = desc.split("x")
    return int(width), int(height)

# Build synthetic video and read binary data into memory (for testing):
#########################################################################
mp4_filename = 'input.mp4'  # the mp4 is used just as reference
#yuv_filename = "/tmp/test_0.out" 

if len(sys.argv) != 4:
    print("Usage: yuv2rgb-cv.py yuvFile width height")
    sys.exit(1)
yuv_filename = sys.argv[1]
width = int(sys.argv[2])
height = int(sys.argv[3])

#fps = 1 # 1Hz (just for testing)
# Build synthetic video, for testing (the mp4 is used just as reference):
#sp.call(['ffmpeg', '-y', 'lavfi', '-i', 'testsrc=size={}x{}:rate=1'.format(width,height), '-vcodec', 'libx264', '-crf', '18', '-t', '10', mp4_filename])
# sp.run(['ffmpeg', '-y', '-f', 'lavfi', '-i', 'testsrc=size={}x{}:rate=1'.format(width, height), '-pix_fmt', 'nv12', '-t', '10', yuv_filename])
#########################################################################

file_size = os.path.getsize(yuv_filename)
print("file_size", file_size)
# Number of frames: in YUV420 frame size in bytes is width*height*1.5 for 420 subsampling.
# NOTE!!! This assumes the frame size won't change midstream, which is perfectly legal!

# NV12 specific  4:2:0
frame_size_y = width * height
print("frame_size_y",frame_size_y)
frame_size_u = frame_size_y // 4    # 2x2 subsampling
frame_size_v = frame_size_u
print("frame_size_u+v",frame_size_u+frame_size_v)

frame_size = frame_size_y + frame_size_u + frame_size_v
print("frame_size",frame_size)

n_frames = file_size // frame_size

# Open 'input.yuv' as a binary file.
f = open(yuv_filename, 'rb')

print("Found ", n_frames, " frames")


def checksum(bytes):
    i = 0
    md5 = hashlib.md5()
    md5.update(bytes)
    return md5.hexdigest()

def checksum(bs: bytes) -> str:
    """Calculates the checksum of a file reading chunks of 64KiB"""
    md5 = hashlib.md5()
    md5.update(bs)
    return md5.hexdigest()

for i in range(n_frames):
    # Read Y, U and V color channels and reshape to height*1.5 x width numpy array
    # height*1.5 -- the height alone accounts for Y, each of U and V
    # are 0.25 times the dimensions of Y. So 0.5 the total area needs
    # to be accounted for. In other words, increase advertised height by
    # 50% to account for all the samples.
    frm = f.read(frame_size)
    print(checksum(frm))
    yuv = np.frombuffer(frm, dtype=np.uint8).reshape(int(height*1.5), width)

    # Convert YUV NV12 to RGB (for testing), applies BT.601 "Limited Range" conversion.
    rgb = cv2.cvtColor(yuv, cv2.COLOR_YUV2RGB_YV12)

    # Convert YUV NV12 to Grayscale
    gray = cv2.cvtColor(yuv, cv2.COLOR_YUV2GRAY_YV12)

    if width >= 3840:
        rgb = cv2.resize(rgb, None, fx=0.5, fy=0.5, interpolation=cv2.INTER_LINEAR)
    # rgb = cv2.resize(rgb, None, fx=1, fy=1, interpolation=cv2.INTER_LINEAR)

    # Show RGB image and Grayscale image for testing
    cv2.imshow('rgb', rgb)
    cv2.waitKey(0)  # Wait a 0.5 second (for testing)


f.close()

cv2.destroyAllWindows()
