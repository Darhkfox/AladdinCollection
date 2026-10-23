"""Render a level's whole BG1 (and BG2 behind) from its data in RAM (the page table / block maps the widescreen
margins are drawn from) with the tiles and palettes in VRAM / CGRAM: python snes_levelmap.py [state ...]
Barrier pages (the camera may not enter) are darkened. Output: tools/qa/snes/map_<state>.png (half size)."""
import sys
import os
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa
from PIL import Image

LAYERS = (0x1738, 0x188E)


def tile_pixels(vram, chr_base, tile):
    adr = (chr_base + tile * 16) & 0x7FFF
    out = np.zeros((8, 8), np.uint8)
    for y in range(8):
        w0 = vram[(adr + y) & 0x7FFF]
        w1 = vram[(adr + y + 8) & 0x7FFF]
        for x in range(8):
            sh = 7 - x
            p = ((w0 >> sh) & 1) | (((w0 >> (sh + 8)) & 1) << 1) | (((w1 >> sh) & 1) << 2) | (((w1 >> (sh + 8)) & 1) << 3)
            out[y, x] = p
    return out


def render(s, name):
    wram = bytes(s.wram[:0x20000])
    rom = bytes(s.rom[:s.rom_size])
    vram = list(s.vram[:0x8000])
    cg = list(s.cgram[:256])
    pal = np.zeros((256, 3), np.uint8)
    for i, c in enumerate(cg):
        pal[i] = [((c & 31) << 3), (((c >> 5) & 31) << 3), (((c >> 10) & 31) << 3)]
    r16 = lambda a: wram[a & 0x1FFFF] | (wram[(a + 1) & 0x1FFFF] << 8)  # noqa: E731

    def romw(bank, a):
        o = ((bank & 0x7F) << 15) | (a & 0x7FFF)
        return rom[o % len(rom)] | (rom[(o + 1) % len(rom)] << 8)
    W = (r16(0x1A04) + 256)
    pages_y = 0
    off = r16(0x1738 + 0x42)
    for py in range(15):
        if any(r16(0x1F000 + off + (py + 1) * 64 + (px + 1) * 2) for px in range(W // 256)):
            pages_y = py + 1
    H = max(256, pages_y * 256)
    regs = s.ppu_regs()
    cache = {}
    img_arr = np.zeros((H, W, 3), np.uint8)
    img_arr[:, :] = pal[0]
    for L in (1, 0):
        base = LAYERS[L]
        off = r16(base + 0x42)
        bank = wram[base + 0x4C]
        chr_base = regs['bg%d' % (L + 1)]['chr']
        for ty in range(H // 8):
            y = ty * 8
            for tx in range(W // 8):
                x = tx * 8
                idx = off + (((y + 256) & 0xFF00) >> 2) + ((((x >> 8) + 1) * 2) & 0xFE)
                pg = r16(0x1F000 + (idx & 0xFFF))
                barrier = pg & 0x8000
                if barrier:
                    pg = 0
                blk = r16(0x10000 + ((pg + (((x >> 3) & 0x1E) | ((y << 1) & 0x1E0))) & 0xFFFF)) | 0x8000
                e = romw(bank, (blk + (((x >> 2) & 2) | ((y >> 1) & 4))) & 0xFFFF)
                t = e & 0x3FF
                key = (chr_base, t)
                if key not in cache:
                    cache[key] = tile_pixels(vram, chr_base, t)
                px = cache[key]
                if e & 0x4000:
                    px = px[:, ::-1]
                if e & 0x8000:
                    px = px[::-1, :]
                p = (e >> 10) & 7
                col = pal[p * 16 + px]
                mask = px != 0
                blk_img = img_arr[y:y + 8, x:x + 8]
                blk_img[mask] = col[mask]
                if barrier and L == 0:
                    blk_img[:] = blk_img // 3
    im = Image.fromarray(img_arr).resize((W // 2, H // 2), Image.BILINEAR)
    out = QA + '/map_%s.png' % name
    im.save(out)
    return out


if __name__ == '__main__':
    names = sys.argv[1:] or [f[:-6] for f in sorted(os.listdir(STATES)) if f.startswith('scan_') and not f.startswith('scan_8')]
    for n in names:
        s = SNES()
        s.load_file(STATES + '/%s.state' % n)
        print(render(s, n))
