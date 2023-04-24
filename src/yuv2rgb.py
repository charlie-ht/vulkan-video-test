__author__ = "Farid Zakaria"
__copyright__ = "Copyright 2011, AMD"
__credits__ = ["Sherin Sasidhan (serin.s@gmail.com)"]
__license__ = "GPL"
__version__ = "1.0."

import argparse
from PIL import Image
import sys
from struct import *
import os
import math

DEFAULT_STRIDE = -1
DEFAULT_FRAME = 0

parser = argparse.ArgumentParser(prog='YUV2RGB Converter', description='Convert YUV Raw images into RGB format')
parser.add_argument('filename', type=str)
parser.add_argument('width', type=int)
parser.add_argument('height', type=int)
parser.add_argument('--stride', default=DEFAULT_STRIDE, type=int, help='stride of the image (default: width of the image')
parser.add_argument('--frame', default=DEFAULT_FRAME, type=int, help='frame number to grab (default: index 0)')
parser.add_argument('--version',action='version', version='%(prog)s 1.0')


def clip(z, x, y):
        if z < x:
                return x;
        elif z > y:
                return y
        else:
                return z


def convert_i420(filename, width, height, stride, frame):
    f_y = open(filename, "rb")
    f_uv= open(filename, "rb")

    converted_image_filename = filename.split('.')[0] + ".bmp"
    converted_image = Image.new("RGB", (width, height) )
    pixels = converted_image.load()

    size_of_file = os.path.getsize(filename)

    size_of_y_data = width * height;
    # size_of_uv_data = width * height  # 4:4:4 sampling (i.e. no sampling)
    # size_of_uv_data = 0.5 * (width + height)  # 4:2:2 sampling
    size_of_uv_data = 0.25 * (width + height)  # 4:2:0 sampling

    size_of_frame = int(size_of_y_data + size_of_uv_data)
    number_of_frames = size_of_file // size_of_frame

    frame_start = size_of_frame * frame
    u_start = frame_start + (width*height)
    v_start = u_start + size_of_uv_data
    
    f_y.seek(int(frame_start));    
    for j in range(0, height):
        for i in range(0, width):
            #uv_index starts at the end of the yframe.  The UV is 1/2 height so we multiply it by j/2
            #We need to floor i/2 to get the start of the UV byte
            uv_index = uv_start + (width * math.floor(j/2)) + (math.floor(i/2))*2
            f_uv.seek(uv_index)
            
            y = ord(f_y.read(1))
            u = ord(f_uv.read(1))
            v = ord(f_uv.read(1))
            
            C = y - 16;
            D = u - 128;
            E = v - 128;
            r = clip((298 * C + 409 * E + 128)           >> 8, 0, 255)
            g = clip((298 * C - 100 * D - 208 * E + 128) >> 8, 0, 255)
            b = clip((298 * C + 516 * D + 128)           >> 8, 0, 255)


            pixels[i,j] = int(r), int(g), int(b)

    converted_image.save(converted_image_filename)


def main():
    args = parser.parse_args()

    if args.stride == DEFAULT_STRIDE:
        args.stride = args.width

    if args.frame < 0:
        args.frame = 0

    convert_i420(args.filename, args.width, args.height, args.stride, args.frame)

if __name__ == "__main__":
    main()
