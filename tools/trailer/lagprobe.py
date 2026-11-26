"""Where the SNES game slows down: snes_lag.py's simple bot (Right, jumping and throwing; invincible) with Remove
Slowdown off, the lag frames' indices per level, and the busiest 4.5 s window: python lagprobe.py <levels> [frames]"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tb  # noqa: F401
from snt import SNES

LOOP = 0x8182CF


def probe(level, frames):
    s = SNES(); s.module('snes'); s.configure(46, True, 'invincible,inf_health,inf_lives')
    s.turbo(int(os.environ.get('TURBO', 1)))
    if not s.start_level(level): return None
    s.bp(LOOP); s.bp_hits()
    lag = []
    for f in range(frames):
        k = f % 90
        pad = 'RIGHT' + ('+B' if 10 <= k < 30 else '+Y' if 50 <= k < 54 else '')
        s.hold(pad); s.run(1)
        if s.info()['gameplay'] and not s.bp_hits(): lag.append(f)
    best = max(((sum(1 for q in lag if a <= q < a + 270), a) for a in range(0, frames, 15)), default=(0, 0))
    return lag, best


if __name__ == '__main__':
    frames = int(sys.argv[2]) if len(sys.argv) > 2 else 2400
    for lv in [int(x) for x in sys.argv[1].split(',')]:
        r = probe(lv, frames)
        if r: print(lv, len(r[0]), 'lag; busiest 4.5 s from frame %d: %d' % (r[1][1], r[1][0]), r[0][:60], flush=True)
