#!/usr/bin/env python3

import sys
import struct
import logging

import utils

logging.basicConfig(level=logging.DEBUG)
log = logging.getLogger('unfs')

def main():
    fsname = sys.argv[1]
    dirname = sys.argv[2]

    index = []
    with open(sys.argv[1], 'rb') as f:
        # uint32_t = entry_count
        entry_count = struct.unpack('<i', f.read(4))[0]
        log.debug('entry_count = %d' % entry_count)

        for _ in range(entry_count):
            # char[13] = filename, including dot, right-zero-padded
            # uint32_t = offset from the beginning of the FS file
            # uint32_t = length
            fname_raw, offset, length = struct.unpack('<13sLL', f.read(21))
            fname = utils.from_c_string(fname_raw)
            index.append((fname, offset, length))

        for fname, offset, length in index:
            log.debug('unpacking %s: %d +%d bytes' % (fname, offset, length))
            assert f.tell() == offset
            payload = f.read(length)
            with open('%s/%s' % (dirname, fname), 'wb') as g:
                g.write(payload)

if __name__ == '__main__':
    main()
