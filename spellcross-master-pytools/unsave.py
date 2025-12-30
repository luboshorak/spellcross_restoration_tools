#!/usr/bin/env python3

import sys
import struct
import logging

import unlz
import utils

logging.basicConfig(level=logging.DEBUG)
log = logging.getLogger('unsave')

def main():
    with open(sys.argv[1], 'rb') as f:
        lz = unlz.LZFile(f)

        header = struct.unpack('<90H', lz.read(180))
        print('header: ', header)

        while True:
            buf = lz.read(66)
            if buf is None:
                break

            name_raw, garbage = struct.unpack('<30s36s', buf)
            name = utils.from_c_string(name_raw)

            print('%s: %d (%s)' % (name, len(garbage), garbage))

            ## UNFINISHED ##

if __name__ == '__main__':
    main()
