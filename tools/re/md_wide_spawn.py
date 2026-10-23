"""Object spawn positions in the widened view (Aladdin MD): walks each level right then left and logs where objects
appear (screen x / y of a slot that became used, in the original's screen coordinates) and whether that is inside the
widened view as slid on that frame ([-E+s, 320+E+s) x [0, 224)). Objects should appear outside it (or where the
original shows them appearing inside its 4:3 view: thrown apples, projectiles, things that drop in).

    python tools/re/md_wide_spawn.py <ext> [entry ...]
"""
import sys
from md_util import *

OBJ, N, SIZE = 0xFF7E40, 32, 0x42


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def spawns(ext, entry, script=((240, 'RIGHT'), (60, 'RIGHT+C'), (240, 'RIGHT'), (300, 'LEFT'))):
    md = ModuleMD(ROM, 'md')
    md.configure(ext)
    start_play(md, entry, 1)
    out = []
    prev = [md.ram8(OBJ + i * SIZE) for i in range(N)]
    for f, b in script:
        md.hold(b)
        for _ in range(f):
            md.mrun(1)
            camx, camy = s16(md.ram16(0xFF7DF6)), s16(md.ram16(0xFF7DF8))
            vs = md.view()[0]
            for i in range(1, N):
                a = OBJ + i * SIZE
                cur = md.ram8(a)
                if cur and not prev[i] and not (md.ram8(a + 6) & 8):
                    out.append((b, s16(md.ram16(a + 2)) - camx, s16(md.ram16(a + 4)) - camy - 240, vs))
                prev[i] = cur
    return out


if __name__ == '__main__':
    ext = int(sys.argv[1])
    entries = [int(a) for a in sys.argv[2:]] or range(13)
    for e in entries:
        sp = spawns(ext, e)
        inside = [s for s in sp if -ext + s[3] <= s[1] < 320 + ext + s[3] and 0 <= s[2] < 224]
        print('entry %d ext %d: %d spawns, %d inside the view: %s' % (e, ext, len(sp), len(inside),
              ' '.join('%s:%d,%d(s%d)' % (s[0][0], s[1], s[2], s[3]) for s in inside[:30])))
