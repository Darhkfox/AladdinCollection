"""Attract mode at every width: from power-on, no input, N frames. The demos replay recorded input, so they must
play exactly as at 4:3: the work RAM is compared with the widescreen-off run frame by frame (a hash per frame), and
the modes the run goes through are listed (a demo ending in a game over, mode $11, would be a soft-lock).

    python sms_attract.py [ext ...] [--frames 60000] [--cheats invincible]
"""
import argparse
import hashlib
import sms_re as R
from concurrent.futures import ProcessPoolExecutor


def run(args):
    ext, frames, cheats = args
    s = R.boot()
    s.module('sms')
    s.configure(ext, True, cheats)
    s.set_ext(ext)
    hashes = []
    modes = []
    last = None
    for f in range(frames):
        s.mframes(1, None)
        ram = bytes(s.ram)
        hashes.append(hashlib.md5(ram).digest()[:8])
        m = (ram[0x200], ram[0x104], ram[0x105])
        if m != last:
            modes.append((f, '%02x/%02x/%d' % m))
            last = m
    return ext, hashes, modes


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('ext', nargs='*', type=int, default=[0, 6, 22, 40, 74, 26, 44, 64, 102])
    ap.add_argument('--frames', type=int, default=60000)
    ap.add_argument('--cheats', default='')
    a = ap.parse_args()
    exts = a.ext if 0 in a.ext else [0] + a.ext
    with ProcessPoolExecutor(len(exts)) as ex:
        res = {e: (h, m) for e, h, m in ex.map(run, [(e, a.frames, a.cheats) for e in exts])}
    ref = res[0][0]
    for e in exts:
        h, m = res[e]
        diff = [f for f in range(len(h)) if h[f] != ref[f]]
        demos = sum(1 for f, x in m if x[3:5] not in ('00', 'ff') and x[:2] == x[3:5])
        stuck = [f for f, x in m if x.startswith('11/') and x[3:5] != '00']
        print('E=%3d: %d frames, %d demos started, game overs in a demo: %s, RAM differs from E=0 in %d frames%s' % (
            e, len(h), demos, stuck or 'none', len(diff), ' (first %d)' % diff[0] if diff else ''), flush=True)
    print('E=0 modes:', res[0][1][:40])
