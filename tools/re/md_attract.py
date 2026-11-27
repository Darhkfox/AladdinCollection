"""Attract mode at every width (Aladdin MD): from power-on, no input, N frames. The demos replay recorded input, so
they must play exactly as at 4:3 without cheats: the 68000 RAM (and the Z80's) is compared with the widescreen-off,
no-cheat run frame by frame (a hash per frame), and the demos each run goes through are listed (level, first and end
frame, the input pointer at the demo's end).

    python tools/re/md_attract.py [ext ...] [--frames 40000] [--cheats invincible,inf_health,...] [--story-fresh]

With --cheats every width also runs with those cheats on, against the same reference. With --story-fresh every run but
the reference has the option Stories: Skip Only on a New Press on (GameConfig::story_fresh_press; the app's default).
"""
import argparse
import hashlib
from concurrent.futures import ProcessPoolExecutor
from md_util import *


def run(args):
    ext, frames, cheats, fresh = args
    md = ModuleMD(ROM, 'md')
    md.story_fresh_press(fresh)
    md.configure(ext, True, cheats)
    hashes, zhashes = [], []
    demos, cur = [], None
    for f in range(frames):
        md.mrun(1)
        ram = bytes(md.ram)
        hashes.append(hashlib.md5(ram).digest()[:8])
        zhashes.append(hashlib.md5(bytes(md.zram)).digest()[:8])
        d = ram[0xF57C]
        if d and cur is None:
            cur = [f, None, ram[0x7E26], 0]
        elif not d and cur is not None:
            cur[1] = f
            demos.append(tuple(cur))
            cur = None
        if cur is not None:
            cur[3] = int.from_bytes(ram[0xF576:0xF57A], 'big')
    return args, hashes, zhashes, demos


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('ext', nargs='*', type=int, default=[20, 32, 40, 54, 64, 80, 108, 128])
    ap.add_argument('--frames', type=int, default=40000)
    ap.add_argument('--cheats', default='')
    ap.add_argument('--story-fresh', action='store_true')
    a = ap.parse_args()
    cheats = tuple(c for c in a.cheats.split(',') if c)
    fr = a.story_fresh
    jobs = [(0, a.frames, (), False)] + [(e, a.frames, (), fr) for e in ([0] if fr else []) + [e for e in a.ext if e]]
    if cheats:
        jobs += [(e, a.frames, cheats, fr) for e in [0] + [e for e in a.ext if e]]
    with ProcessPoolExecutor(min(len(jobs), 8)) as ex:
        out = list(ex.map(run, jobs))
    ref = out[0]
    print('E=0: demos (level, first frame, end frame, input pointer at the end):',
          [(hex(l), f0, f1, hex(p)) for f0, f1, l, p in ref[3]])
    for (e, _, ch, _fr), h, zh, demos in out[1:]:
        diff = [f for f in range(len(h)) if h[f] != ref[1][f]]
        zdiff = [f for f in range(len(zh)) if zh[f] != ref[2][f]]
        print('E=%3d%s: %d frames, %d demos (%s the reference), 68000 RAM differs in %d frames%s, Z80 RAM in %d' % (
            e, (' with cheats' if ch else '') + (' (story option on)' if _fr else ''), len(h), len(demos), 'same as' if demos == ref[3] else 'NOT as', len(diff),
            ' (first %d)' % diff[0] if diff else '', len(zdiff)), flush=True)
