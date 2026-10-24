"""4:3 parity (Aladdin MD): the module at width 0, no cheats, against the bare core with the same random input - RAM, VRAM
and the picture compared on every frame.

    python tools/re/md_parity.py [--frames 6000] [--seed 7] [entries ...]

Runs from power-on (the title, a new game, play: 12,000 frames) and from the first frame of play of each level given
(default: every level select entry, the Rug Ride included; the state is made by the module's level select at width 0
and loaded into both).
"""
import argparse
import random
from concurrent.futures import ProcessPoolExecutor
from md_util import *

BTNS = ['', 'RIGHT', 'LEFT', 'RIGHT+C', 'C', 'A', 'B', 'START', 'DOWN', 'UP', 'RIGHT+A', 'UP+C', 'DOWN+B']


def run(job):
    entry, frames, seed = job
    rnd = random.Random(seed * 100 + entry + 1)
    a = ModuleMD(ROM, 'md')
    b = MD(ROM)
    if entry < 0:
        a.reset()
        a.configure(0)
        b.reset()
    else:
        a.configure(0)
        if not start_play(a, entry, 1):
            return entry, 0, ['no play']
        st = a.save()
        a.load(st)
        a.configure(0)
        b.load(st)
    diffs, f = [], 0
    while f < frames:
        btn = rnd.choice(BTNS) if entry >= 0 or f > 600 else ('START' if (f // 20) % 2 else '')
        if btn == 'START' and entry >= 0 and rnd.random() < 0.8:
            btn = ''
        n = rnd.randrange(1, 25)
        a.hold(btn)
        b.hold(btn)
        for _ in range(n):
            a.mrun(1)
            b.run(1)
            f += 1
            if bytes(a.ram) != bytes(b.ram) or bytes(a.vram) != bytes(b.vram) or a.pixels()[2] != b.pixels()[2]:
                diffs.append(f)
    return entry, f, diffs


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('entries', nargs='*', type=int, default=list(range(13)))
    ap.add_argument('--frames', type=int, default=6000)
    ap.add_argument('--seed', type=int, default=7)
    a = ap.parse_args()
    jobs = [(-1, 12000, a.seed)] + [(e, a.frames, a.seed) for e in a.entries]
    with ProcessPoolExecutor(8) as ex:
        for entry, n, diffs in ex.map(run, jobs):
            print('%s: %d frames, %d differ%s' % ('power-on' if entry < 0 else 'entry %2d' % entry, n, len(diffs),
                                                  ' (first %s)' % diffs[:3] if diffs else ''), flush=True)
