"""Widescreen helpers for Aladdin (MD): the level map in RAM and a plane-A checker.

Map (docs/re_md_wide.md): blocks of 16x16 px; the row pointer of block row r is the long at $FF9884 + 64*r (the init
writes it 16 times); the row holds one word per block column: a byte offset into the block definitions at ($FF7DBE)
(4 name-table words: TL, TR, BL, BR). Screen pixel (x, line) shows map pixel (x + camX + 16, line + camY + 16):
camX = $FF7DF6, camY = $FF7DF8. Plane A ($C000, 64x32 cells) holds map x mod 512, map y mod 256.
"""
from md_util import *

CAM_X, CAM_Y = 0xFF7DF6, 0xFF7DF8
STREAM_X, STREAM_Y = 0xFF7E06, 0xFF7E08


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def map_dims(md):
    return md.ram16(0xFF7DB6), md.ram16(0xFF7DBA)   # width, height in blocks


def map_cell(md, mx, my):
    """expected name-table word for map pixel (mx, my), or None outside the map"""
    W, H = map_dims(md)
    bx, by = mx >> 4, my >> 4
    if mx < 0 or my < 0 or bx >= W or by >= H:
        return None
    row = md.ram32(0xFF9884 + 64 * by)
    blk = md.ram16(row + 2 * bx)
    defs = md.ram32(0xFF7DBE)
    off = defs + blk + ((my >> 3) & 1) * 4 + ((mx >> 3) & 1) * 2
    if defs >= 0xFF0000:
        return md.ram16(off)
    return r16(off)


def plane_cell(md, px, py, base=0xC000):
    a = base + ((py >> 3) & 31) * 128 + ((px >> 3) & 63) * 2
    v = md.vram
    return (v[a] << 8) | v[a + 1]


def map_plane(md):
    """the plane the level map is streamed to: plane B when $FFF165 is set (planes swapped), else A"""
    return 'B' if md.ram8(0xFFF165) else 'A'


def view_origin(md, line, plane=None):
    """map (x, y) shown at screen (0, line), from the scroll values the VDP uses and the streamed range
    ($FF7E06 / $FF7E08: the plane holds map x in [L, L+368), map y in [Y, Y+256))"""
    plane = plane or map_plane(md)
    v = md.vram
    hsb = (md.regs[13] & 0x3F) << 10
    mode = md.regs[11] & 3
    off = 0 if mode == 0 else (line & 7) * 4 if mode == 1 else (line & ~7) * 4 if mode == 2 else line * 4
    a = hsb + off + (2 if plane == 'B' else 0)
    hs = (v[a] << 8 | v[a + 1]) & 0x3FF
    vs = md.vsram[1 if plane == 'B' else 0] & 0x3FF
    L, Y = md.ram16(STREAM_X), md.ram16(STREAM_Y)
    mx0 = L + ((-hs - L) & 511)
    if mx0 >= L + 256 + 128:
        mx0 -= 512
    my = Y + ((line + vs - Y) & 255)
    return mx0, my


def check_plane(md, E, lines=(0, 224), verbose=False):
    """compare every 8x8 cell of the map plane visible in the widened view (screen x -E..319+E) with the map;
    returns (bad cells in the extension, bad cells in the centre, outside-map cells)"""
    plane = map_plane(md)
    base = 0xE000 if plane == 'B' else 0xC000
    bad_ext = bad_mid = outside = 0
    for line in range(lines[0], lines[1], 8):
        mx0, my = view_origin(md, line, plane)
        for sx in range(-E, 320 + E, 8):
            mx = mx0 + sx
            want = map_cell(md, mx, my)
            if want is None:
                outside += 1
                continue
            got = plane_cell(md, mx, my, base)
            if got != want:
                if 0 <= sx < 320:
                    bad_mid += 1
                else:
                    bad_ext += 1
                if verbose:
                    print('bad sx=%d line=%d map=(%d,%d) got %04x want %04x' % (sx, line, mx, my, got, want))
    return bad_ext, bad_mid, outside
