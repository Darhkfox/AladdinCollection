"""Decode a streaming BG layer's tile entries straight from the level data (the column builder $81:C2E0's lookup) and
compare them with VRAM over the game's own screen, for every level state given (default: the scan states).

Layer structs at $1738 (BG1) and $188E (BG2), stride $156: +$3B x, +$3F y (world), +$42 offset into the page table
$7FF000, +$4C the metatile bank, +$2E VRAM map base. Page table: 32 entries (2 bytes) per page row, row/column -1 as a
border; an entry is the page's block map in bank $7F (16x16 blocks of 16 px, 2 bytes each), bit 15 = a page the camera
may not enter (drawn as page 0). Block entry | $8000 = the metatile's 4 tile entries (TL, TR, BL, BR) in the bank."""
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa

LAYERS = (0x1738, 0x188E)


def entry(s, base, wx, wy):
    off = s.r16(base + 0x42)
    idx = off + ((wy + 256) >> 8) * 64 + (((wx >> 8) + 1) * 2 & 0xFE)
    page = s.r16(0x7FF000 + (idx & 0xFFF))
    if page & 0x8000:
        page = 0
    blk_off = ((wx >> 3) & 0x1E) | ((wy << 1) & 0x1E0)
    blk = s.r16(0x7F0000 + ((page + blk_off) & 0xFFFF)) | 0x8000
    sub = ((wx >> 2) & 2) | ((wy >> 1) & 4)
    bank = s.r8(base + 0x4C)
    return s.r16((bank << 16) | ((blk + sub) & 0xFFFF))


def vram_entry(s, mapbase, tx, ty):
    off = ((ty & 31) << 5) | (tx & 31)
    off += (tx & 32) << 5
    return s.vram[(mapbase + off) & 0x7FFF]


def compare(s, L):
    base = LAYERS[L]
    regs = s.ppu_regs()['bg%d' % (L + 1)]
    mapbase = regs['map']
    # the scroll on screen: the register's 10 bits, completed from the struct's x / y (it may lag the struct a frame)
    sx, sy = s.s16(base + 0x3B), s.s16(base + 0x3F)
    cx = sx + ((regs['hofs'] - sx + 512) & 0x3FF) - 512
    cy = sy + ((regs['vofs'] - sy + 512) & 0x3FF) - 512
    bad = 0
    tot = 0
    for ty in range(1, 28):
        for tx in range(0, 32):
            wx = cx + tx * 8
            wy = cy + ty * 8
            e = entry(s, base, wx, wy)
            v = vram_entry(s, mapbase, (regs['hofs'] + tx * 8) >> 3, (regs['vofs'] + ty * 8) >> 3)
            tot += 1
            if e != v:
                bad += 1
    return bad, tot


if __name__ == '__main__':
    names = sys.argv[1:] or [f[5:-6] for f in sorted(os.listdir(STATES)) if f.startswith('scan_')]
    for n in names:
        s = SNES()
        s.load_file(STATES + '/scan_%s.state' % n)
        print(n, 'BG1', compare(s, 0), 'BG2', compare(s, 1))
