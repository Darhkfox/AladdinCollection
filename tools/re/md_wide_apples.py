"""The apples that come while Aladdin has none (map type $61, four from one block; docs/re_md_wide.md section 3, Objects
that keep their map block) in the boss arenas: Aladdin, out of apples, walks to each $61 block of the arena, jumps for
the apples (picked-up apples set back to "00", as if thrown at the boss), walks to a wall, and again four times - the
same goal-driven input at every width. Reports the apples picked up (the level's starting 10 included), the sets
spawned, the frames with apples alive, the most alive at once and the apples at each jump spot as Aladdin arrives
(the 4:3 game: a whole set wherever it has one); a 2x2 grid of shots per width in md_out.

    python tools/re/md_wide_apples.py <jafar|quarters|idol> [ext ...]
"""
import os
import sys
from md_util import *
from md_wide_util import s16
from concurrent.futures import ProcessPoolExecutor

OBJ, SIZE = 0xFF7E40, 0x42

# entry, map-view flight (button, until (camx, camy)), route: ('j', x) jump under apples at x, ('g', x) go to x
SCEN = {
    'jafar': (12, [('RIGHT', lambda x, y: x >= 560), ('DOWN', lambda x, y: y >= 560)],
              [('j', 672), ('g', 480), ('j', 896), ('g', 1130)] * 4, (700, 1100, 1700, 2300)),
    'quarters': (11, [], [('j', 256), ('g', 30), ('j', 496), ('g', 780)] * 4, (300, 900, 1500, 2100)),
    'idol': (5, [('UP', lambda x, y: y <= 300), ('DOWN', lambda x, y: y >= 300), ('RIGHT', lambda x, y: x >= 1960)],
             [('j', 2064), ('g', 2050), ('j', 2272), ('g', 2300)] * 4, (300, 900, 1500, 2100)),
}


def cam(md):
    return s16(md.ram16(0xFF7DF6)), s16(md.ram16(0xFF7DF8))


def apples(md):
    return [(i, s16(md.ram16(OBJ + i * SIZE + 2)), s16(md.ram16(OBJ + i * SIZE + 4))) for i in range(1, 32)
            if md.ram8(OBJ + i * SIZE) and md.ram8(OBJ + i * SIZE + 0x34) == 0x61]


def run(job):
    E, scen = job
    entry, fly, route, shots = SCEN[scen]
    md = ModuleMD(ROM, 'md')
    md.configure(E, True, ('invincible',))
    start_play(md, entry, 1)
    md.mrun(10)
    if fly:
        md.set8(0xFF7273, 0xFF)
        for b, cond in fly:
            for _ in range(3000):
                md.hold(b); md.set8(0xFF7273, 0xFF); md.mrun(1)
                if cond(*cam(md)):
                    break
        md.hold(0); md.set8(0xFF7273, 0xFF); md.mrun(2)
        md.set8(0xFF7273, 0)
        md.set8(0xFF7E40, 0x83)
    md.mrun(60)
    st = dict(frame=0, prev=[], picked=0, alive=0, spawns=0, maxn=0)
    pics, visits = [], []

    def tick(b):
        md.hold(b)
        v = md.ram16(0xFFEFE0)
        if v != 0x3030:   # picked up: out of apples again (as if thrown)
            st['picked'] += ((v >> 8) - 48) * 10 + (v & 255) - 48
            md.set16(0xFFEFE0, 0x3030)
        md.mrun(1)
        st['frame'] += 1
        cur = apples(md)
        if [c for c in cur if c[0] not in {p[0] for p in st['prev']}]:
            st['spawns'] += 1
        st['prev'] = cur
        st['alive'] += bool(cur)
        st['maxn'] = max(st['maxn'], len(cur))
        if st['frame'] in shots:
            pics.append(shot(md))

    def go(tx):
        last, still = None, 0
        for _ in range(900):
            x = s16(md.ram16(0xFF7E42))
            if abs(x - tx) < 6 or still > 30:
                break
            still = still + 1 if x == last else 0
            last = x
            tick('RIGHT' if x < tx else 'LEFT')
        for _ in range(8):
            tick(0)

    for k, x in route:
        go(x)
        if k == 'j':
            ax = s16(md.ram16(0xFF7E42))
            visits.append(sum(1 for _, px, py in apples(md) if abs(px - ax - 16) < 40))
            for _ in range(3):
                for _ in range(30):
                    tick('C')
                for _ in range(20):
                    tick(0)
    while len(pics) < len(shots):
        pics.append(shot(md))
    return E, scen, st, visits, pics, shots


if __name__ == '__main__':
    scen = sys.argv[1] if len(sys.argv) > 1 else 'jafar'
    exts = [int(a) for a in sys.argv[2:]] or [0, 54, 128]
    with ProcessPoolExecutor(8) as ex:
        for E, scen, st, visits, pics, shots in ex.map(run, [(E, scen) for E in exts]):
            grid(pics, cols=2, labels=['%s E=%d frame %d' % (scen, E, f) for f in shots],
                 path=os.path.join(OUT, 'wide_apples_%s_%d.png' % (scen, E)))
            print('%-8s E=%3d: %d frames, apples picked up %d, apple-set spawns %d, frames with apples alive %d, '
                  'most alive %d; apples at the jump spots on arrival %s' % (scen, E, st['frame'], st['picked'],
                  st['spawns'], st['alive'], st['maxn'], visits), flush=True)
