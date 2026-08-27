#!/usr/bin/env python3
"""Convert a PPM (QEMU screendump output) to PNG using only stdlib (zlib).

Usage: ppm2png.py input.ppm [output.png]
"""
import struct
import sys
import zlib

def read_ppm(path):
    with open(path, 'rb') as f:
        data = f.read()
    # P6 header: "P6\n<w> <h>\n255\n" then raw RGB
    if not data.startswith(b'P6'):
        raise ValueError('not a P6 PPM')
    parts = data.split(b'\n', 3)
    if len(parts) < 4:
        raise ValueError('malformed PPM header')
    dims = parts[1].split()
    w, h = int(dims[0]), int(dims[1])
    maxval = int(parts[2])
    if maxval != 255:
        raise ValueError(f'unexpected maxval {maxval}')
    rgb = parts[3]
    return w, h, rgb

def write_png(path, w, h, rgb):
    def chunk(typ, payload):
        c = struct.pack('>I', len(payload)) + typ + payload
        c += struct.pack('>I', zlib.crc32(typ + payload) & 0xffffffff)
        return c

    ihdr = struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)  # 8-bit RGB
    # raw scanlines with filter byte 0
    stride = w * 3
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgb[y * stride:(y + 1) * stride]
    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', ihdr)
           + chunk(b'IDAT', zlib.compress(bytes(raw), 9))
           + chunk(b'IEND', b''))
    with open(path, 'wb') as f:
        f.write(png)
    print(f'wrote {path} ({w}x{h})')

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print('usage: ppm2png.py input.ppm [output.png]')
        sys.exit(1)
    w, h, rgb = read_ppm(sys.argv[1])
    out = sys.argv[2] if len(sys.argv) > 2 else sys.argv[1].rsplit('.', 1)[0] + '.png'
    write_png(out, w, h, rgb)
