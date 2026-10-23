"""The sound uploads' block lists ($85:8000 table; $80:B4D0's format: per block a word count and an ARAM address, the
data; a count of 1 ends the list): python snes_uploads.py"""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa

s = SNES()
for i in list(range(0, 3)) + list(range(0x10, 0x37)):
    p = s.r24(0x858000 + i * 3)
    bank = (p >> 16) | 0x80
    y = p & 0x7FFF
    blocks = []
    for k in range(40):
        def rd(o):
            off = y + o
            b = bank + (off >> 15)
            return s.r16((b << 16) | 0x8000 | (off & 0x7FFF))
        count = rd(0)
        dest = rd(2)
        blocks.append('%04X+%X' % (dest, count))
        if count <= 1:
            break
        y += 4 + count
    print('%02X %06X: %s' % (i, p, ' '.join(blocks)))
