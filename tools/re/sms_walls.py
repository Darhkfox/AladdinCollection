"""The walls stages 0 and 1 animate with the camera (docs/re_sms_wide.md section 4a): each wall in the view, frame to
frame, taken at its place in the level (background only). A sudden change of a whole wall (a face appearing at once,
scraps of other tiles, a wall changing while the view holds still) is a pop; the tables' own steps change a few
columns at a time.

    python sms_walls.py <ext> <stage> [--frames 3700] [--limit 600]

Prints the sudden changes (frame, wall's world x, its screen x, pixels changed) and those with the wall in a side.
"""
import argparse
import numpy as np
import sms_re as R
from sms_wide_shots import Player

# the walls' first columns and widths, and the lines they cover
WALLS = {
    0: ([(x, 16) for k in (0, 1, 2, 4, 5, 6, 7) for x in (64 + 512 * k, 176 + 512 * k)], (16, 152)),
    1: ([(x, 32) for x in (1088, 1136, 1632, 1664, 2144, 2176, 2656, 2688, 3680, 3712)], (130, 176)),
}


def run(ext, stage, frames, limit):
    walls, (l0, l1) = WALLS[stage]
    s = R.boot()
    s.module('sms')
    s.configure(ext, True, 'invincible')
    s.set_ext(ext)
    s.start_level(stage)
    s.layers(1)
    p = Player(s, stage, retry=False)
    prev, pops, seen = {}, [], 0
    for f in range(frames):
        p.step()
        if not p.in_stage():
            break
        px, w, h = s.pixels()
        a = np.array(px, dtype=np.uint32).reshape(h, w)
        c = s.ram16(0xC482)
        lc = (-s.line_hscroll()[l0 + 8]) & 0xFF   # (the camera the band's lines were drawn with)
        cam = c + ((((lc - c) & 0xFF) + 128) & 0xFF) - 128
        left = cam - ext + s.view_shift(l0 + 8)   # (world x at the picture's left edge)
        cur = {}
        for x0, ww in walls:
            o = x0 - left
            if o < 0 or o + ww > w:
                continue
            blk = a[l0:l1, o:o + ww]
            cur[x0] = blk
            seen += 1
            if x0 in prev:
                d = int((prev[x0] != blk).sum())
                if d > limit:
                    pops.append((f, x0, o - ext, d))
        prev = cur
    side = [q for q in pops if q[2] < 0 or q[2] + 16 > 256]
    print('stage %d E=%d: %d wall-frames in view, sudden changes (> %d px): %d, with the wall (partly) in a side: %d %s;'
          ' largest %s' % (stage, ext, seen, limit, len(pops), len(side), side[:10],
                           max(pops, key=lambda q: q[3]) if pops else None), flush=True)


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('ext', type=int)
    ap.add_argument('stage', type=int, choices=(0, 1))
    ap.add_argument('--frames', type=int, default=3700)
    ap.add_argument('--limit', type=int, default=600)
    a = ap.parse_args()
    run(a.ext, a.stage, a.frames, a.limit)
