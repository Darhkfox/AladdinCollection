"""Where objects wake and go to sleep relative to the view as it is shown, per stage and width (the stages played by
sms_wide_shots.Player, the free-scrolling ones driven by its camera patch).

    python sms_wakes.py [ext ...] [--stages ...] [--frames 3000]

The view in screen coordinates is [-E + s, 256 + E + s) (s: the slide). For each wake: the distance from the view's
nearer edge, outside (+) or inside (-); a wake or a sleep inside the view (more than 8 px in, lines 16-175) is a
pop-in / pop-out (a sleep at its last position awake). Queue stages (0 1 5 7 8): the camera at which each list record wakes, against E = 0 (how much
earlier: the earliness).
"""
import argparse
import collections
import sms_re as R
from sms_wide_shots import Player, drive, FREE

QUEUE = (0, 1, 5, 7, 8)


def s16(v):
    return v - 65536 if v & 0x8000 else v


def run(ext, st, frames):
    s = R.boot()
    s.module('sms')
    s.configure(ext, True, 'invincible')
    s.set_ext(ext)
    s.start_level(st)
    if st in FREE:
        drive(s, 2)
    p = Player(s, st, retry=False)
    n = s.peek(0x3D14) if st in QUEUE else 16   # (the queue stages: 7 slots, 12 in a wide view)
    prev = [0] * n
    last = [None] * n   # (view x, y) while awake: where it was when it went
    wakes, sleeps, cams = [], [], {}
    for f in range(frames):
        p.step()
        if not p.in_stage():
            break
        vs = s.view_shift(-1)
        for i in range(n):
            o = 0xCA00 + i * 0x40
            aw = s.ram8(o + 0x1D)
            x, y = s16(s.ram16(o + 2)), s16(s.ram16(o))
            t = s.ram8(o + 0x15)
            if aw and not prev[i]:
                v = x - vs   # view coordinates: [-E, 256 + E)
                d = v - (256 + ext) if v >= 128 else -ext - v
                wakes.append((f, t, v, y, d))
                if st in QUEUE:
                    cams.setdefault((t, s.ram16(o + 0x28)), s.ram16(0xC482))
            if prev[i] and not aw and last[i]:
                sleeps.append((f, t) + last[i])
            if aw:
                last[i] = (x - vs, y)
            prev[i] = aw
    return wakes, sleeps, cams


def inside(v, y, ext):
    return -ext + 8 < v < 256 + ext - 8 and 16 < y < 176


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('ext', nargs='*', type=int, default=[74, 102])
    ap.add_argument('--stages', default='0,1,2,3,4,5,6,7,8,9')
    ap.add_argument('--frames', type=int, default=3000)
    a = ap.parse_args()
    for st in [int(x) for x in a.stages.split(',')]:
        base = run(0, st, a.frames)
        for e in [0] + a.ext:
            wakes, sleeps, cams = base if e == 0 else run(e, st, a.frames)
            pin = [(f, '%02X' % t, v, y) for f, t, v, y, d in wakes if inside(v, y, e)]
            pout = [(f, '%02X' % t, v, y) for f, t, v, y in sleeps if inside(v, y, e)]
            dist = collections.Counter(min(max(d // 16 * 16, -64), 256) for f, t, v, y, d in wakes)
            line = 'stage %d E=%3d: %3d wakes, outside the view by (px, 16-px bins) %s; inside the view: %d woke %s, %d slept %s' % (
                st, e, len(wakes), sorted(dist.items()), len(pin), pin[:4], len(pout), pout[:4])
            if st in QUEUE and e:
                early = collections.Counter()
                for k, c in cams.items():
                    if k in base[2]:
                        early[base[2][k] - c] += 1
                line += '; earlier than at 4:3 (camera px: records) %s' % sorted(early.items())
            print(line, flush=True)
