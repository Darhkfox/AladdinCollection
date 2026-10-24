"""The Cave of Wonders' river (Aladdin MD, docs/re_md_wide.md section 3, Objects that keep their map block): the
stepping stones ($EF, blocks 132/144/156/165/175 at row 59) spawn through $1B52A0, which leaves their map block, so
the 4:3 game spawns one whenever its column comes into the plane window. A map-view pass over the river (camera Y
782), back and forth by 50-110 px; after each move: the stones alive per x (copies on one block), the objects alive,
and the stones whose column is inside the slid view but have no object (missing).

    python tools/re/md_wide_stones.py [ext ...]
"""
import collections
import sys
from concurrent.futures import ProcessPoolExecutor
from md_util import *
from md_wide_util import s16

STONES = [132, 144, 156, 165, 175]
PASS = [1900, 2000, 1950, 2050, 2000, 2100, 2050, 2150, 2100, 2200, 2150, 2250, 2200, 2300, 2400, 2350, 2500, 2450, 2600]


def cam(md):
    return s16(md.ram16(0xFF7DF6)), s16(md.ram16(0xFF7DF8))


def fly_to(md, tx, ty):
    for _ in range(3000):
        cx, cy = cam(md)
        b = []
        if cx < tx - 7: b.append('RIGHT')
        elif cx > tx + 7: b.append('LEFT')
        if cy < ty - 7: b.append('DOWN')
        elif cy > ty + 7: b.append('UP')
        if not b:
            break
        md.hold('+'.join(b))
        md.set8(0xFF7273, 0xFF)
        md.mrun(1)
    md.hold(0)
    md.set8(0xFF7273, 0xFF)
    md.mrun(2)


def run(E):
    md = ModuleMD(ROM, 'md')
    md.configure(E, True, ('invincible',))
    start_play(md, 5, 1)
    md.set8(0xFF7273, 0xFF)
    most, objs, missing = 0, 0, []
    for tx in PASS:
        fly_to(md, tx, 782)
        c = collections.Counter()
        n = 0
        for k in range(1, 32):
            a = 0xFF7E40 + k * 0x42
            if md.ram8(a):
                n += 1
                if md.ram8(a + 0x34) == 0xEF:
                    c[s16(md.ram16(a + 2))] += 1
        camx, s = cam(md)[0], (md.view()[0] if E else 0)
        lo, hi = camx + 16 - E + s, camx + 336 + E + s
        missing += [(camx, 16 * b - 16) for b in STONES if lo <= 16 * b and 16 * b + 32 <= hi and not c[16 * b - 16]]
        most, objs = max([most] + list(c.values())), max(objs, n)
    return E, most, objs, missing


if __name__ == '__main__':
    exts = [int(a) for a in sys.argv[1:]] or [0, 54, 128]
    with ProcessPoolExecutor(8) as ex:
        for E, most, objs, missing in ex.map(run, exts):
            print('E=%3d: at most %d stones on one block, %d objects alive at most; stones in view without their object: '
                  '%d %s' % (E, most, objs, len(missing), missing[:6]), flush=True)
