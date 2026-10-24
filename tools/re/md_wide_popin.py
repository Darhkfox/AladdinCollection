"""Pop-in check (Aladdin MD): where map objects first show in the widened, slid view. An object counts once it has
lived 2 frames (a spawn that can't get sprite tiles is gone the next frame and never drawn); its screen x / y on its
first frame is compared with the slid view [-E+s, 320+E+s) x [0, 224). Also counts the sprite-tile allocation failures
that drop an object ($1AD40A, after the failure path: the pool of $74 slots at $FFF008 shared by every object; the
module's hook at $1AD406 makes room first, so no breakpoint goes there).

    python tools/re/md_wide_popin.py <ext> [entries]
"""
import sys
from md_util import *

OBJ, N, SIZE = 0xFF7E40, 32, 0x42


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def run(ext, entry, script=((240, 'RIGHT'), (60, 'RIGHT+C'), (300, 'RIGHT'), (200, 'LEFT'), (200, 'RIGHT'))):
    md = ModuleMD(ROM, 'md')
    md.configure(ext)
    start_play(md, entry, 1)
    md.bp(0x1AD40A)
    born = {}          # slot -> (frame, x, y, s, type) of a new object
    inside, fails = [], 0
    prev = [md.ram8(OBJ + i * SIZE) for i in range(N)]
    frame = 0
    for f, b in script:
        md.hold(b)
        for _ in range(f):
            md.mrun(1)
            frame += 1
            fails += len(md.bp_hits())
            camx, camy = s16(md.ram16(0xFF7DF6)), s16(md.ram16(0xFF7DF8))
            vs = md.view()[0]
            for i in range(1, N):
                a = OBJ + i * SIZE
                cur = md.ram8(a)
                if cur and not prev[i] and not (md.ram8(a + 6) & 8):
                    born[i] = (frame, s16(md.ram16(a + 2)) - camx, s16(md.ram16(a + 4)) - camy - 240, vs,
                               md.ram8(a + 0x34))
                elif cur and i in born and frame - born[i][0] == 1:
                    fr, x, y, s, t = born.pop(i)
                    if -ext + s <= x < 320 + ext + s and 0 <= y < 224 and t:   # (t: map objects only)
                        inside.append((b, x, y, s, '%02x' % t))
                elif not cur:
                    born.pop(i, None)
                prev[i] = cur
    return inside, fails


if __name__ == '__main__':
    ext = int(sys.argv[1])
    for e in [int(a) for a in sys.argv[2:]] or [0, 2, 3, 4, 5, 7, 9, 10, 11, 12]:
        inside, fails = run(ext, e)
        print('entry %d ext %d: %d map objects first seen inside the view, %d tile allocation failures: %s' %
              (e, ext, len(inside), fails, ' '.join('%s:%d,%d(s%d,%s)' % (i[0][0], i[1], i[2], i[3], i[4]) for i in inside[:16])))
