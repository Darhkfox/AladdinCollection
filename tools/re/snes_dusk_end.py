"""The Market at Dusk's end at every width: python snes_dusk_end.py [exts] [seeds]   (default 0,46,106,140 and 1)

Each run (invincible, infinite lives and apples) reaches the arena, fights the boss and then walks right; it reports
when the boss and Jasmine (objects $57 / $56) came out, when the boss was beaten ($00BD) and when the level ended
(STAGE CLEAR, index $24). Runs: every width x Remove Slowdown off / on x three ways there (carry: Aladdin moved 200 px
every 90 frames; walk: the play bot; slow: short steps, throwing) x four fights (a: jumping onto him, b: apples facing
him, c: apples from the left wall, r: random input) x seeds. The apples don't hurt him (b and c never beat him, at 4:3
too). Every run's outcome is compared with the 4:3 run's. Runs in parallel processes."""
import itertools
import os
import random
import sys
from concurrent.futures import ProcessPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def run(job):
    ext, turbo, approach, style, seed = job
    from snes_common import SNES
    from snes_wide_play import bot
    rnd = random.Random(seed * 1000 + ord(style))
    s = SNES()
    s.module('snes')
    s.turbo(4 if turbo else 1)
    s.configure(ext, True, 'invincible,inf_lives,inf_apples')
    s.start_level(3)
    s.run(30)

    def present(t):
        return any(s.r16(0x092B + i * 0x50 + 6) & 0xFF == t and s.r8(0x092B + i * 0x50) for i in range(1, 35))

    def boss_x():
        for i in range(1, 35):
            b = 0x092B + i * 0x50
            if s.r8(b) and s.r16(b + 6) & 0xFF == 0x57:
                return s.r16(b + 0x0C)
        return 1424

    f, y = 0, s.r16(0x8EA)
    while f < 6000 and s.r16(0x1A42) != 0x500:      # to the arena's start (the camera taken to $500)
        if approach == 'carry':
            if f % 90 == 0 and s.r16(0x1773) < 0x3C0:
                s.w16(0x8E7, s.r16(0x8E7) + 200)
                s.w16(0x8EA, y)
            s.hold('RIGHT')
            s.run(1)
            f += 1
        elif approach == 'walk':
            bot(s, 60, 10 ** 9, rnd, [], [], '')
            f += 60
        else:
            for b, n in [('RIGHT', 6), ('', 10), ('Y', 4), ('', 6)]:
                s.hold(b)
                s.run(n)
                f += n
    boss = jas = beaten = ended = None
    g = 0
    while g < 9000:
        if boss is None and present(0x57):
            boss = g
        if jas is None and present(0x56):
            jas = g
        if not s.r8(0xBD):
            d = 'RIGHT' if boss_x() > s.r16(0x8E7) else 'LEFT'
            if style == 'a':
                seq = [(d + '+B', 18), (d, 10), ('Y', 4)]
            elif style == 'b':
                seq = [(d, 3), ('Y', 6), ('', 6)]
            elif style == 'c':
                seq = [('LEFT', 8), ('RIGHT', 1), ('Y', 6), ('', 6)] if s.r16(0x8E7) > 1300 else [('RIGHT', 1), ('Y', 6), ('', 8)]
            else:
                seq = [(rnd.choice(['RIGHT', 'LEFT', 'RIGHT+B', 'LEFT+B', 'Y', 'B', 'RIGHT+Y', '']), rnd.randint(3, 20))]
        else:
            if beaten is None:
                beaten = g
            seq = [('RIGHT', 30), ('RIGHT+B', 15)] if rnd.random() < 0.7 else [('LEFT', 20), ('RIGHT', 40)]
        for b, n in seq:
            s.hold(b)
            s.run(n)
            g += n
        if s.r8(0x35E) != 3:
            ended = g
            break
        if beaten is not None and g > beaten + 3000:
            break
    return job, (f, boss, jas, beaten, ended, s.r16(0x1773), s.r16(0x8E7))


if __name__ == '__main__':
    exts = [int(e) for e in (sys.argv[1] if len(sys.argv) > 1 else '0,46,106,140').split(',')]
    seeds = [int(e) for e in (sys.argv[2] if len(sys.argv) > 2 else '1').split(',')]
    if 0 not in exts:
        exts = [0] + exts
    jobs = list(itertools.product(exts, [0, 1], ['carry', 'walk', 'slow'], 'abcr', seeds))
    with ProcessPoolExecutor(min(16, os.cpu_count() or 4)) as ex:
        res = dict(ex.map(run, jobs))
    for ext in exts:
        rs = [(j, r) for j, r in res.items() if j[0] == ext]
        beaten = sum(r[3] is not None for _, r in rs)
        ended = sum(r[4] is not None for _, r in rs)
        other = [j for j, r in rs if ((r[3] is None) != (res[(0,) + j[1:]][3] is None) or
                                      (r[4] is None) != (res[(0,) + j[1:]][4] is None))]
        print('ext %3d: %d runs, boss beaten in %d, level ended in %d; outcome unlike 4:3 in %d' % (
            ext, len(rs), beaten, ended, len(other)), flush=True)
        for j in other:
            r = res[j]
            print('   %s: arena f%d, boss %s, Jasmine %s, beaten %s, end %s (camera %d, x %d)' % ((j,) + r))
