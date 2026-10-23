"""Slowdown in SNES Aladdin: per level, the share of gameplay frames in which the level's main loop ($81:82CF) didn't
start (a lag frame), with play driven by a simple bot (Right held, jumping and throwing now and then; invincible).

    python snes_lag.py [ext] [turbo] [frames] [levels]

turbo: the CPU speed for the game's frame work (the app's Remove Slowdown uses 4; 1 = the original).
Also prints the main loop's iterations per gameplay frame when nothing is lagging (a level built to run below 60 fps
shows fewer than 1 even when idle).
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from snt import SNES  # noqa: E402

LOOP = 0x8182CF


def run_level(level, ext, turbo, frames):
    s = SNES()
    s.module('snes')
    s.configure(ext, True, 'invincible,inf_health,inf_lives')
    if turbo > 1:
        s.turbo(turbo)
    if not s.start_level(level):
        return None
    s.bp(LOOP)
    gameplay = []
    for f in range(frames):
        k = f % 90
        pad = 'RIGHT'
        if 10 <= k < 30:
            pad += '+B'
        elif 50 <= k < 54:
            pad += '+Y'
        s.hold(pad)
        s.run(1)
        gameplay.append((s.frame, s.info()['gameplay']))
    hits = {}
    for h in s.bp_hits(200000):
        hits[h['frame']] = hits.get(h['frame'], 0) + 1
    play = [fr for fr, g in gameplay if g]
    lag = sum(1 for fr in play if hits.get(fr, 0) == 0)
    iters = sum(hits.get(fr, 0) for fr in play)
    return len(play), lag, iters


def main():
    ext = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    turbo = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    frames = int(sys.argv[3]) if len(sys.argv) > 3 else 1200
    levels = [int(x) for x in sys.argv[4].split(',')] if len(sys.argv) > 4 else list(range(19))
    tot_play = tot_lag = 0
    for lv in levels:
        r = run_level(lv, ext, turbo, frames)
        if r is None:
            print('level %2d: did not start' % lv)
            continue
        play, lag, iters = r
        tot_play += play
        tot_lag += lag
        print('level %2d: %4d gameplay frames, %4d lag (%5.1f%%), %.2f iterations per frame'
              % (lv, play, lag, 100.0 * lag / max(1, play), iters / max(1, play)))
    print('all: %.1f%% lag' % (100.0 * tot_lag / max(1, tot_play)))


if __name__ == '__main__':
    main()
