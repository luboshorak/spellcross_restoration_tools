#!/usr/bin/env python3

import sys
import struct
import logging

logging.basicConfig(level=logging.INFO)
log = logging.getLogger('unlz')

# quick'n'dirty bitstream reader
class Bits:
    def __init__(self, f):
        self.f = f
        self.avail = 0
        self.byte = None

    def read(self, nbits):
        """ Read nbits bits and return them MSB-first in an int.
            Return None if EOF occurs while reading nbits.
        """

        result = 0
        while nbits > 0:
            if self.avail >= nbits:
                result <<= nbits
                result |= (self.byte >> (self.avail - nbits)) & ((1 << nbits) - 1)

                self.avail -= nbits
                return result
            else:
                if self.avail > 0:
                    result <<= self.avail
                    result |= self.byte & ((1 << self.avail) - 1)
                    nbits -= self.avail

                bs = self.f.read(1)
                if bs == b'':
                    return None

                #log.debug(nbits, bs)
                self.byte = bs[0]
                self.avail = 8

        return result

class LZFile:
    def __init__(self, f):
        self.iter = iter_unpack(f)
        self.buf = b''

    def read(self, nbytes):
        result = b''

        while True:
            if len(self.buf) >= nbytes:
                result += self.buf[:nbytes]
                self.buf = self.buf[nbytes:]
                return result
            else:
                result += self.buf
                nbytes -= len(self.buf)
                try:
                    self.buf = next(self.iter)
                except StopIteration:
                    return None

    def __iter__(self):
        return self.iter


# The algorithm follows
# https://en.wikipedia.org/wiki/Lzw
def iter_unpack(f):
    # uint16_t = initial dictionary size
    # uint8_t  = initial bits per symbol
    dict_size, bits_per_symbol = struct.unpack('<HB', f.read(3))

    log.debug('dict_size = %d, bits per symbol = %d' % (dict_size, bits_per_symbol))

    if dict_size < 256:
        # the usual case
        # dict_size * uint8_t = initial dictionary
        dictionary = []
        for byte in f.read(dict_size):
            dictionary.append(bytes([byte]))
    else:
        # if the dictionary contains all bytes,
        # we needn't store it -- so it's not present in the file
        dictionary = [bytes([byte]) for byte in range(256)]

    # add a special symbol
    # dictionary[dict_size] = CLEAR
    # 2xCLEAR == EOF
    dictionary.append(None)

    # make a copy of the dictionary
    # in case we get CLEAR
    clear_dictionary = list(dictionary)
    clear_bits_per_symbol = bits_per_symbol

    # wrap the file in a bit reader
    br = Bits(f)
    previous_is_clear = False
    while True:

        # read symbol
        i = br.read(bits_per_symbol)
        if i is None:
            break  # EOF

        # This is a special case in LZW decompression:
        # we're emitting a symbol that we haven't completed yet.
        # Whenever this happens, the last letter must be the same as the first.
        if i == len(dictionary)-1:
            if dictionary[-1] is None:
                # CLEAR symbol, nothing to do
                pass
            else:
                dictionary[-1] = dictionary[-1][:-1] + bytes([dictionary[-1][0]])

        symbol = dictionary[i]
        log.debug('dict[%d] == %s' % (i, symbol))

        if symbol is None:
            if previous_is_clear:
                # this is EOF
                break
            else:
                # just a CLEAR
                dictionary = list(clear_dictionary)
                bits_per_symbol = clear_bits_per_symbol
                previous_is_clear = True
                continue
        else:
            previous_is_clear = False

        # complete the previous symbol
        if dictionary[-1] is not None:
            dictionary[-1] = dictionary[-1][:-1] + bytes([symbol[0]])
            
        # add an incomplete current symbol
        dictionary.append(symbol + b'\0')

        # check if we need more bits per symbol from now on
        if len(dictionary)+1 > (1 << bits_per_symbol):
            bits_per_symbol += 1

        yield symbol

def unpack(f):
    return b''.join(iter_unpack(f))

if __name__ == '__main__':
    with open(sys.argv[1], 'rb') as f:
        for chunk in iter_unpack(f):
            sys.stdout.buffer.write(chunk)
