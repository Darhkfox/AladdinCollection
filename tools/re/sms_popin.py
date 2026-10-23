"""Object pop-in check: where objects first become visible, with and without widescreen.

    python sms_popin.py [ext] [--stages 0,1,5,7,8] [--frames 3000]

Runs each stage (level-select states sms_ls<n>.state, invincible; the free-scrolling stages with the camera
driven by the test patch of sms_wide_shots) and records, for every object slot of the $CA00 table, the screen x
of the first frame it is awake (+$1D) and drawn. A pop-in is a first appearance inside the view:
x in (8 - E, 248 + E). Also counts frames where the sprite allocator had to skip an object.
"""
import argparse
import sms_re as R
from sms_wide_shots import drive, FREE

N_SLOTS = {0: 7, 1: 7, 5: 7, 7: 7, 8: 7}


def sw(v):
    return v - 65536 if v & 0x8000 else v


def run(ext, st, frames, verbose=False):
    s = R.boot()
    s.module('sms')
    s.configure(ext, True, 'invincible')
    s.set_ext(ext)
    s.start_level(st)
    if st in FREE:
        drive(s, 2)
    y0 = s.ram8(0xC400)
    n = N_SLOTS.get(st, 16)
    awake = [False] * n
    pops, spawns = [], 0
    s.bp(0x3D86)   # allocator full: an object not drawn
    s.bp(0x3D6B)
    for f in range(frames):
        if st in FREE:
            s.set8(0xC400, y0)
        s.mframes(1, 'RIGHT' if (f // 60) % 4 != 3 else 'RIGHT+2')
        if not (2 <= s.ram8(0xC200) <= 0x0B):
            break
        for i in range(n):
            a = 0xCA00 + i * 0x40
            on = s.ram8(a + 0x1D) != 0
            if on and not awake[i]:
                x = sw(s.ram16(a + 2))
                spawns += 1
                if 8 - ext < x < 248 + ext:
                    pops.append((f, i, s.ram8(a + 0x15), x, s.ram8(a + 0x20), s.ram16(0xC482)))
            awake[i] = on
    return spawns, pops, len(s.bp_log())


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('ext', nargs='*', type=int, default=[0, 40, 102])
    ap.add_argument('--stages', default='0,1,2,3,4,5,6,7,8')
    ap.add_argument('--frames', type=int, default=3000)
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    for st in [int(x) for x in a.stages.split(',')]:
        for e in a.ext:
            spawns, pops, full = run(e, st, a.frames)
            print('stage %d E=%3d: %3d wakes, %2d inside the view, %4d objects not drawn' % (st, e, spawns, len(pops), full),
                  '' if not a.v else ' '.join('f%d s%d t%02X x%d sc%d cam%d' % p for p in pops))
