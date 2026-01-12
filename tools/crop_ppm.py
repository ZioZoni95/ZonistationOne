#!/usr/bin/env python3
import sys
from pathlib import Path

if len(sys.argv) < 6:
    print("Usage: crop_ppm.py input.ppm out.ppm x y w h")
    sys.exit(2)

infile = Path(sys.argv[1])
outfile = Path(sys.argv[2])
x = int(sys.argv[3])
y = int(sys.argv[4])
w = int(sys.argv[5])
h = int(sys.argv[6]) if len(sys.argv) > 6 else 0

if not infile.exists():
    print("Input not found:", infile)
    sys.exit(1)

with infile.open('rb') as f:
    header = f.readline().decode('ascii')
    if not header.startswith('P6'):
        print('Not a P6 PPM')
        sys.exit(1)
    # read width/height line
    dims = f.readline().decode('ascii')
    while dims.startswith('#'):
        dims = f.readline().decode('ascii')
    parts = dims.split()
    width = int(parts[0]); height = int(parts[1])
    maxval = int(f.readline().decode('ascii'))
    if maxval != 255:
        print('Unexpected maxval', maxval)
    data = f.read()

if w == 0 or h == 0:
    print('Invalid w/h')
    sys.exit(1)

if x < 0 or y < 0 or x + w > width or y + h > height:
    print('Crop out of bounds:', x,y,w,h,'image',width,height)
    sys.exit(1)

out = bytearray()
for row in range(y, y+h):
    start = (row * width + x) * 3
    out += data[start:start + w*3]

with outfile.open('wb') as f:
    f.write(b'P6\n')
    f.write(f'{w} {h}\n'.encode('ascii'))
    f.write(b'255\n')
    f.write(out)

print('Wrote', outfile)
