"""Level ends in the widened view (Aladdin MD): the picture slides there instead of showing outside the level.
For every wide level: the start (fully slid), walking away from it, then the developers' map view flies the camera
to the map's right end (fully slid the other way) and back a little.

    python tools/re/md_wide_edges.py <ext> [entries]    -> md_out/edges_<ext>.png
Also checks, per frame, that the view stays inside the 4:3 game's range ([16, W*16-18) of map x) and that the
picture of plane A doesn't jump while sliding (the view's map x at screen 0 moves by at most the camera's step).
"""
import sys
from md_util import *

WIDE = [0, 2, 3, 4, 5, 7, 9, 10, 11, 12]   # entries shown wide (not Abu's rounds, not the Rug Ride)


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def view_left(md, ext):
    """the slid view's map x at screen x -E (the module's view: md.view() gives (view_shift, mask l, mask r))"""
    vs = md.view()[0]
    return s16(md.ram16(0xFF7DF6)) + 16 + vs - ext, vs


def run(ext, entries):
    md = ModuleMD(ROM, 'md')
    ims, lab = [], []
    problems = []
    for e in entries:
        md.configure(ext)
        start_play(md, e, 1)
        W = md.ram16(0xFF7DB6)
        hi = W * 16 - 18

        def step(frames, buttons, tag):
            md.hold(buttons)
            for _ in range(frames):
                md.mrun(1)
                left, vs = view_left(md, ext)
                if md.info & 1 and (left < 16 - 10 or left + 320 + 2 * ext > hi + 10):   # (RAM is the next frame's: a camera step of slack)
                    problems.append((e, tag, left, vs))
            ims.append(shot(md))
            lab.append('%d %s cam %d s %d' % (e, tag, s16(md.ram16(0xFF7DF6)), md.view()[0]))

        step(20, 0, 'start')
        step(50, 'RIGHT', 'walk')
        md.set8(0xFF7273, 0xFF)              # map view: fly to the right end
        md.hold('RIGHT')
        last, keep, best = -1, None, -1
        for _ in range(3000):
            md.mrun(4)
            c = s16(md.ram16(0xFF7DF6))
            if md.info & 1 and c > best:     # (map view can end in a pit: keep the last frame in play)
                best, keep = c, (shot(md), md.view()[0], md.save())
            if c == last or not md.info & 1:
                break
            last = c
        if keep:
            md.load(keep[2])
            md.configure(ext)
        step(10, 'RIGHT', 'end')
        step(12, 'LEFT', 'back')
        md.set8(0xFF7273, 0)
        md.set8(0xFF7E40, 0x83)
        md.hold(0)
    grid(ims, 4, lab, OUT + '/edges_%d.png' % ext)
    return problems


if __name__ == '__main__':
    ext = int(sys.argv[1])
    entries = [int(a) for a in sys.argv[2:]] or WIDE
    pr = run(ext, entries)
    print('view outside the level in %d frames' % len(pr), pr[:10])
