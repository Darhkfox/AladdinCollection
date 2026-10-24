"""Attract mode at every width: from power-on, no input, N frames. The demos replay recorded input ($0331 = 2, input
from $84:F700), so they must play exactly as the original: the work RAM is compared with widescreen off and Remove
Slowdown off frame by frame (a hash per frame), and the screens the run goes through are listed.

    python snes_attract.py [config ...] [--frames 60000] [--cheats invincible,inf_health,...]

A config is <ext> or <ext>t<turbo> (e.g. 140t4: 21:9 square with Remove Slowdown). Default: 0 46 106 140 0t4 140t4.
The reference is always 0 (4:3, turbo 1, no cheats). Runs in parallel processes."""
import argparse
import hashlib
import os
import sys
from concurrent.futures import ProcessPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def run(args):
    conf, frames, cheats = args
    from snes_common import SNES
    ext, _, turbo = conf.partition('t')
    ext, turbo = int(ext), int(turbo or 1)
    s = SNES()
    s.module('snes')
    s.configure(ext, True, cheats)
    s.turbo(turbo)
    hashes, screens, demo_frames, wide_demo = [], [], 0, 0
    last = None
    for f in range(frames):
        s.run(1)
        ram = s.wram_bytes()
        hashes.append(hashlib.md5(ram).digest()[:8])
        demo = ram[0x331] == 2
        demo_frames += demo
        if demo and s.info()['gameplay']:
            wide_demo += 1          # (a demo frame reported as gameplay: it would be shown wide)
        sc = (ram[0x35E], demo)
        if sc != last:
            screens.append('%02X%s@%d' % (sc[0], 'd' if demo else '', f))
            last = sc
    return conf, cheats, hashes, screens, demo_frames, wide_demo


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('conf', nargs='*', default=['0', '46', '106', '140', '0t4', '140t4'])
    ap.add_argument('--frames', type=int, default=60000)
    ap.add_argument('--cheats', default='')
    a = ap.parse_args()
    jobs = [('0', a.frames, '')] + [(c, a.frames, a.cheats) for c in a.conf if not (c == '0' and not a.cheats)]
    with ProcessPoolExecutor(len(jobs)) as ex:
        res = list(ex.map(run, jobs))
    ref = res[0][2]
    for conf, cheats, h, screens, demo_frames, wide_demo in res:
        diff = [f for f in range(len(h)) if h[f] != ref[f]]
        print('%-6s %s: %d frames, %d in a demo (%d shown wide), RAM differs from 4:3 in %d frames%s' % (
            conf, cheats or 'no cheats', len(h), demo_frames, wide_demo, len(diff),
            ' (first %d)' % diff[0] if diff else ''), flush=True)
    print('screens (4:3):', ' '.join(res[0][3][:60]))
