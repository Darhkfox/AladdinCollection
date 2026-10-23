"""Print every level's page table ($7FF000: per BG layer, a grid of 256x256-px pages, 32 per row, one row and column of
border; bit 15 = a page the camera may not enter) with the camera limits ($1A04 / $1A06, $1A40 / $1A42)."""
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa

names = sys.argv[1:] or ['%d_%d' % p for p in [(0, 0), (0, 1), (0, 2), (0, 3), (1, 0), (1, 1), (2, 0), (2, 1), (3, 0),
                                                 (3, 1), (3, 2), (4, 0), (4, 1), (4, 2), (5, 0), (5, 1), (5, 2), (5, 3),
                                                 (6, 0), (7, 0)]]
for n in names:
    s = SNES()
    s.load_file(STATES + '/scan_%s.state' % n)
    print('== %s level %02X  cam %d,%d  max %04X,%04X  lim %04X..%04X  lock %d %d' % (
        n, s.r8(0x35E), s.r16(0x1773), s.r16(0x1777), s.r16(0x1A04), s.r16(0x1A06), s.r16(0x1A40), s.r16(0x1A42),
        s.r16(0x1A10), s.r16(0x1A12)))
    for L, base in ((0, 0x1738), (1, 0x188E)):
        off = s.r16(base + 0x42)
        print(' BG%d off %04X cam %d,%d chr/meta bank %02X flags %02X' % (L + 1, off, s.r16(base + 0x3B), s.r16(base + 0x3F),
                                                                        s.r8(base + 0x4C), s.r8(base + 0x4F)))
        for r in range(0, 16):
            row = [s.r16(0x7FF000 + off + r * 64 + 2 * c) for c in range(32)]
            if r > 0 and all(v == 0 for v in row):
                continue
            print('  y%2d ' % (r - 1) + ' '.join(('#' if v & 0x8000 else '.') + '%03X' % ((v & 0x7FFF) >> 4) for v in row[:24]))
