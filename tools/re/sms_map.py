"""Aladdin (SMS) level maps: the name-table entry of any world cell, as the game's column streamer builds it
(docs/re_sms_wide.md), and a checker against what the game wrote to VRAM.

Engine A/C (paged, $2E19): page table at ($C48F) (one word per 256 px), 16 metatile columns per page row,
  rows ($C489) bytes apart; metatiles ($C49D) 8 bytes: TL TR BL BR.
Engine B (flat, $2DEC): one map, ($C489) bytes per metatile row, from world x ($C4B9) on.
The map and metatiles are read with slot 2 = bank table $3306[$C105].
"""
import sms_re as R

ENGINE = {0: 'A', 1: 'A', 8: 'A', 5: 'A', 7: 'A', 2: 'B', 3: 'B', 4: 'B', 6: 'B', 9: 'B'}


def bank_of(s):
    return R.rom()[0x3306 + s.ram8(0xC105)]


def rd8(bank, a):
    return R.rom()[bank * 0x4000 + (a & 0x3FFF)]


def rd16(bank, a):
    return rd8(bank, a) | (rd8(bank, a + 1) << 8)


def entry(s, wx, row, paged=None):
    """name-table entry of world pixel column wx (any x in the cell), playfield cell row `row` (0 = VRAM row 2)"""
    st = s.ram8(0xC105)
    bank = bank_of(s)
    if paged is None:
        paged = ENGINE.get(st) == 'A'
    stride = s.ram16(0xC489)
    if paged:
        if wx < 0:
            return None
        base = rd16(bank, s.ram16(0xC48F) + 2 * (wx >> 8))
        mt = rd8(bank, base + ((wx & 255) >> 4) + (row >> 1) * stride)
    else:
        mx = (wx - s.ram16(0xC4B9)) >> 4
        if mx < 0 or mx >= stride:
            return None
        mt = rd8(bank, s.ram16(0xC48F) + mx + (row >> 1) * stride)
    return rd16(bank, s.ram16(0xC49D) + mt * 8 + (row & 1) * 4 + ((wx >> 3) & 1) * 2)


def vram_entry(s, col, row):
    v = s.vram
    a = 0x3800 + (row * 32 + (col & 31)) * 2
    return v[a] | (v[a + 1] << 8)


def check(s, rows=range(20)):
    """compares the name table with the map for the columns the camera shows (after the frame's VBlank):
    returns [(world col, row, vram, map)] mismatches"""
    cam = s.ram16(0xC482)
    k = cam >> 3
    bad = []
    for c in range(k + 1, k + 32):
        for r in rows:
            e = entry(s, c * 8, r)
            v = vram_entry(s, c, r + 2)
            if e is not None and e != v:
                bad.append((c, r, v, e))
    return bad
