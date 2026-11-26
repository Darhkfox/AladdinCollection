"""Enemies coming into the wide view: where sprites appear (and vanish) inside it, and the Sultan's palace guards
against 4:3 (docs/re_sms_wide.md section 3, "The Sultan's palace guards").

    python sms_guards.py scan <ext> [--stages 0,...,9] [--subs 6.0,6.1,6.2,2.1,2.2,2.3,4.0] [--frames 3000]
    python sms_guards.py compare <app state> ... [--exts 74,102] [--frames 2600]

scan: every stage played as sms_wide_shots.Player plays it (the free-scrolling ones driven right, then left, by its
camera patch), and the free-scrolling stages' other sub-areas (entered by setting $C4A0 from the stage's start: 1 one
up, 2 one down) driven right and left. Each frame is drawn with the sprites only; sprite pixels with nothing within
12 px the frame before are a pop-in (with nothing there the frame after, a pop-out), listed when more than 10 px
inside the picture's edges and reaching more than 8 px into a side (L / R: the left / right side), with the figures and objects
near them. A pose change of a figure already shown, an object's own blink or a projectile set off is listed too:
look at them (the 4:3 part's are the original's and not listed).
compare: app save states (the user's: core state, module part, tag), from the load at 4:3 and the given widths with
the same input (a story scene's text tapped through, then right, stand, right with jumps, left): Aladdin's and the
second figure's X / Y / state, health and the catch per frame; the guards' wakes and the hits.
"""
import argparse
import os
import struct
import sys
import numpy as np
from scipy import ndimage
import sms_re as R
from smst import SMS
from sms_wide_shots import Player, drive, FREE

RAD = 12


def s16(v):
    return v - 65536 if v >= 32768 else v


def sprite_mask(s):
    px, w, h = s.pixels()
    a = np.array(px, dtype=np.uint32).reshape(h, w)[16:176]
    vals, cnt = np.unique(a, return_counts=True)
    m = np.zeros((h, w), bool)
    m[16:176] = a != vals[np.argmax(cnt)]
    return m


def near(s):
    """the second figure (fig + its type) and the awake objects (their type), at their screen X"""
    out = []
    if s.ram8(0xC698) and s.ram8(0xC687) != 0xC0:
        out.append(('fig%X' % s.ram8(0xC698), s16(s.ram16(0xC689))))
    for i in range(16):
        b = 0xCA00 + i * 0x40
        if s.ram8(b + 0x15) and s.ram8(b + 0x1D):
            out.append(('%02X' % s.ram8(b + 0x15), s16(s.ram16(b + 2))))
    return out


def scan(s, E, frames, step, tag):
    s.layers(2)
    prev, events = None, []
    for f in range(frames):
        step(f)
        if not (2 <= s.ram8(0xC200) <= 0x0B):
            break
        m = sprite_mask(s)
        if prev is not None and m.shape == prev.shape:
            sh = s.view_shift(-1)
            lo43 = E - sh   # (the picture is drawn at screen x + E - the slide)
            for kind, a, b in (('in', m, prev), ('out', prev, m)):
                lab, n = ndimage.label(a & ~ndimage.binary_dilation(b, iterations=RAD), structure=np.ones((3, 3)))
                for k, (ys, xs) in enumerate(ndimage.find_objects(lab) if n else []):
                    if (lab[ys, xs] == k + 1).sum() < 24 or xs.start <= 10 or xs.stop >= m.shape[1] - 10:
                        continue
                    side = 'L' if xs.start < lo43 - 8 else 'R' if xs.stop > lo43 + 264 else ''   # (begins in a side)
                    if side:
                        x0, x1 = xs.start - lo43, xs.stop - lo43   # (screen x)
                        who = [o for o in near(s) if x0 - 48 <= o[1] <= x1 + 8]
                        events.append((tag, kind, f, side, x0, ys.start, s.ram16(0xC482), who))
        prev = m
    s.layers(3)
    return events


def boot(ext, st):
    s = R.boot()
    s.module('sms')
    s.configure(ext, True, 'invincible')
    s.set_ext(ext, False)
    s.start_level(st)
    return s


def run_stage(st, E, frames):
    s = boot(E, st)
    if st in FREE:
        drive(s, 2)
    p = Player(s, st, retry=True)
    ev = scan(s, E, frames, lambda f: p.step(), '%d>' % st)
    if st in FREE:
        drive(s, -2)
        y0 = s.ram8(0xC400)

        def left(f):
            s.set8(0xC400, y0)
            s.mframes(1, 'LEFT' if (f // 50) % 4 != 3 else 'LEFT+2')
        ev += scan(s, E, frames, left, '%d<' % st)
    return ev


def run_sub(st, sub, E, frames=1300):
    s = boot(E, st)
    tries = 0
    while s.ram8(0xC49F) != sub:
        was, before = s.ram8(0xC49F), (s.save(), s.save_ext())
        s.set8(0xC4A0, 1 if sub < was else 2)
        for _ in range(120):
            s.mframes(1, 0)
        if s.ram8(0xC49F) == was or s.ram8(0xC200) != st + 2:
            # (not taken here - the palace's start, say: a few steps further in)
            s.load(before[0])
            s.load_ext(before[1])
            tries += 1
            if tries > 10:
                return [('%d.%d' % (st, sub), 'not reached', 0, '', 0, 0, 0, [])]
            for _ in range(60):
                s.mframes(1, 'RIGHT')
    drive(s, 2)
    y0 = s.ram8(0xC400)
    ev = []
    for d, pad in ((2, 'RIGHT'), (-2, 'LEFT')):
        drive(s, d)

        def step(f, pad=pad):
            s.set8(0xC400, y0)
            s.mframes(1, pad)
        ev += scan(s, E, frames, step, '%d.%d%s' % (st, sub, '>' if d > 0 else '<'))
    return ev


# ---- the user's states against 4:3 ----
def load_app_state(path, ext, cheats=None):
    d = open(path, 'rb').read()
    n = d[-1]
    tag = d[-1 - n:-1].decode()
    end = len(d) - 1 - n
    ml = struct.unpack('<I', d[end - 4:end])[0]
    end -= 4
    _, e0, ch = tag.split('|')[:3]
    s = SMS()
    s.module('sms')
    s.configure(int(e0), True, ch)
    s.load(d[:end - ml])
    s.load_ext(d[end - ml:end])
    s.configure(ext, True, ch if cheats is None else cheats)
    return s


def pad_for(scene):
    def pad(f):
        if scene and f < 834:
            return '1' if f % 8 < 2 else 0
        g = f - 834 if scene else f + 120
        if g < 120:
            return 0
        if g < 330:
            return 'RIGHT'
        if g < 560:
            return 0
        if g < 1400:
            return 'RIGHT' if (g // 40) % 5 != 4 else 'RIGHT+2'
        return 'LEFT' if g < 1700 else 'RIGHT'
    return pad


NAMES = ('mode', 'cam', 'alX', 'alY', 'health', 'caught', 'gtype', 'gX', 'gY', 'gstate', 'gtimer')


def play(path, ext, cheats, frames):
    s = load_app_state(path, ext, cheats)
    pad = pad_for(s.ram8(0xC200) == 0x13)
    rows = []
    for f in range(frames):
        s.mframes(1, pad(f))
        rows.append((s.ram8(0xC200), s.ram16(0xC482), s.ram16(0xC402), s.ram8(0xC400), s.ram8(0xC116), s.ram8(0xC438),
                     s.ram8(0xC698), s16(s.ram16(0xC689)), s.ram8(0xC687), s.ram8(0xC6A3), s.ram8(0xC6A4)))
    wakes = [f for f in range(1, frames) if rows[f][8] != 0xC0 and rows[f - 1][8] == 0xC0 and rows[f][6] == rows[f - 1][6]]
    hits = [f for f in range(1, frames) if rows[f][4] < rows[f - 1][4] or (rows[f][5] and not rows[f - 1][5])]
    return rows, wakes, hits


def compare(paths, exts, frames):
    for cheats in (None, ''):
        for path in paths:
            base, w0, h0 = play(path, 0, cheats, frames)
            print('%s (cheats %s): 4:3 wakes %s, hits / catches %s' % (os.path.basename(path), 'as saved' if cheats is None else 'none', w0, h0))
            for e in exts:
                rows, w, h = play(path, e, cheats, frames)
                diff = [f for f in range(frames) if rows[f] != base[f]]
                worst = {}
                for f in diff:
                    for k, (a, b) in enumerate(zip(rows[f], base[f])):
                        if a != b:
                            worst[NAMES[k]] = max(worst.get(NAMES[k], 0), abs(a - b))
                print('  E=%d: wakes %s, hits %s - %s; %d frames differ (largest %s)' % (
                    e, w, h, 'as at 4:3' if (w, h) == (w0, h0) else 'NOT as at 4:3', len(diff), worst or '-'))


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=('scan', 'compare'))
    ap.add_argument('args', nargs='+')
    ap.add_argument('--stages', default='0,1,2,3,4,5,6,7,8,9')
    ap.add_argument('--subs', default='6.0,6.1,6.2,2.1,2.2,2.3,4.0')
    ap.add_argument('--frames', type=int, default=0)
    ap.add_argument('--exts', default='74,102')
    a = ap.parse_args()
    if a.cmd == 'scan':
        E = int(a.args[0])
        ev = []
        for st in [int(x) for x in a.stages.split(',') if x]:
            ev += run_stage(st, E, a.frames or 3000)
        for x in [x for x in a.subs.split(',') if x]:
            st, sub = x.split('.')
            ev += run_sub(int(st), int(sub), E)
        for e in ev:
            print(e)
        print('%d events in the sides' % len(ev))
    else:
        compare(a.args, [int(x) for x in a.exts.split(',')], a.frames or 2600)
