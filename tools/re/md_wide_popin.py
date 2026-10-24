"""Pop-in check (Aladdin MD): map objects that appear inside the widened, slid view. The QA's walk (t_popin.py: right
240 frames, right + jump 60, right 240, left 300, right 200) through every wide level; an object counts when it is
new (+$34 its map type, not screen-fixed), still alive the next frame, and then - its position taken on that second
frame, as the first can end inside the spawn code with the slot's old position - lies well inside the slid view
[-E+s, 320+E+s) x [0, 224) (more than 32 px from its sides, 16 from top and bottom), in a frame of play that is shown
(not a respawn being rebuilt behind a black screen).
Run it at 0 too: what the 4:3 game does itself (objects thrown or dropped by others, placed with an offset from their
block, rows streamed above / below) shows there as well. Also counts the objects dropped for want of sprite tiles
($1AD40A, after the failure path; the module's hook at $1AD406 makes room first, so no breakpoint goes there).

    python tools/re/md_wide_popin.py <ext[,ext...]> [entries]
"""
import sys
from concurrent.futures import ProcessPoolExecutor
from md_util import *

OBJ, N, SIZE = 0xFF7E40, 32, 0x42
WALK = ((240, 'RIGHT'), (60, 'RIGHT+C'), (240, 'RIGHT'), (300, 'LEFT'), (200, 'RIGHT'))
WIDE = [0, 2, 3, 4, 5, 7, 9, 10, 11, 12]
EDGE, EDGE_Y = 32, 16   # well inside: more than 32 px from the view's sides (objects spawn up to 31 px out and walk
                        # in), 16 from its top and bottom (rows)


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def run(job):
    ext, entry = job
    md = ModuleMD(ROM, 'md')
    md.configure(ext)
    start_play(md, entry, 1)
    md.bp(0x1AD40A)
    born = {}          # slot -> frame of a new object
    inside, drops = [], 0
    prev = [md.ram8(OBJ + i * SIZE) for i in range(N)]
    frame = 0
    for f, b in WALK:
        md.hold(b)
        for _ in range(f):
            md.mrun(1)
            frame += 1
            drops += len(md.bp_hits())
            shown = md.info & 1
            camx, camy = s16(md.ram16(0xFF7DF6)), s16(md.ram16(0xFF7DF8))
            vs = md.view()[0]
            for i in range(1, N):
                a = OBJ + i * SIZE
                cur = md.ram8(a)
                if cur and not prev[i]:
                    born[i] = (frame, shown)
                elif cur and i in born and frame - born[i][0] == 1:
                    f0, shown0 = born.pop(i)
                    x, y = s16(md.ram16(a + 2)) - camx - 16, s16(md.ram16(a + 4)) - camy - 240
                    t = md.ram8(a + 0x34)
                    edge = min(x - (-ext + vs), 320 + ext + vs - x)
                    if t and not (md.ram8(a + 6) & 8) and shown0 and shown and edge > EDGE and EDGE_Y < y < 224 - EDGE_Y:
                        inside.append((f0, b, x, y, vs, '%02x' % t))
                elif not cur:
                    born.pop(i, None)
                prev[i] = cur
    return ext, entry, inside, drops


if __name__ == '__main__':
    exts = [int(a) for a in sys.argv[1].split(',')]
    entries = [int(a) for a in sys.argv[2:]] or WIDE
    with ProcessPoolExecutor(8) as ex:
        for ext, e, inside, drops in ex.map(run, [(x, e) for e in entries for x in exts]):
            print('entry %2d ext %3d: %d map objects first seen inside the view, %d dropped for want of tiles%s' % (
                e, ext, len(inside), drops, ': ' + ' '.join('f%d %s %d,%d (s%d, %s)' % (i[0], i[1], i[2], i[3], i[4], i[5])
                                                            for i in inside[:12]) if inside else ''), flush=True)
