#!/usr/bin/env python3

from PIL import Image
import argparse
import struct
import logging

logging.basicConfig(level=logging.INFO)
log = logging.getLogger('mkimg')

def main(args):
    w, h = map(int, args.size.split('x'))

    with open(args.input, 'rb') as f:
        image = Image.frombytes(
            'P' if args.palette else 'L',
            (w, h),            
            f.read(),
        )

        if args.palette:
            with open(args.palette, 'rb') as f:
                image.putpalette(list(f.read()))

    with open(args.output, 'wb') as f:
        image.save(f, format=args.format)

if __name__ == '__main__':
    ap = argparse.ArgumentParser('merge RAW images with palette into PNG')
    ap.add_argument('-s', '--size', default='640x480', help='image size [%(default)]')
    ap.add_argument('-p', '--palette', default=None, help='*.PAL file (grayscale if omitted)')
    ap.add_argument('-i', '--input', default='/dev/stdin', help='input [%(default)]')
    ap.add_argument('-o', '--output', default='/dev/stdout', help='output [%(default)]')
    ap.add_argument('-f', '--format', default='PNG', help='output format [%(default)]')
    main(ap.parse_args())
