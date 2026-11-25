"""STAGE CLEAR's Genie in the sides: python snes_clear_genie.py [exts] [levels]

For each stage's last level (default: all seven) the level is started at 4:3, its exit moved to Aladdin, and the
state at STAGE CLEAR's first frame taken; from it three runs in step with the same input (B every 50 frames from 600
frames on, to the next level's start): 4:3, the width, and the width with the sprites switched off. Per frame:
the work RAM of the wide runs against the 4:3 run's (the sides are drawing only), and on STAGE CLEAR's wide frames
the game's 256 px against the 4:3 picture, the sides without sprites one colour (the backdrop), and every sprite
pixel in the sides within the Genie's box (type $8F, x - 32 .. x + 32, as he is this frame or was the last). Listed:
the frames that fail, how far into each side the Genie was drawn and on how many frames; a sheet of his flight to
tools/qa/snes/clear_genie_<ext>_<level>.png."""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa
import numpy as np
from concurrent.futures import ProcessPoolExecutor



def genie_x(s):
    for k in range(35):
        b = 0x092B + k * 0x50
        if s.r8(b) and s.r8(b + 6) == 0x8F and s.r16(b + 8) & 0x4000:
            return s.s16(b + 0x0C)
    return None


def start_state(lv):
    s = SNES()
    s.module('snes')
    s.configure(0, True, 'invincible')
    s.turbo(4)
    s.start_level(lv)
    s.run(60)
    lvl = s.r8(0x35E)
    off = s.r16(0x80EC1E + lvl * 2)
    s.w16(0x80EC1F + off, s.r16(0x08E7))   # (the exit moved to Aladdin)
    s.w16(0x80EC1F + off + 2, s.r16(0x08EA))
    for _ in range(3000):
        if s.r8(0x35E) == 0x24:
            break
        s.run(1)
    return s.save()


def job(arg):
    lv, ext = arg
    st = start_state(lv)
    runs = []
    for e, mask in ((0, 0x1F), (ext, 0x1F), (ext, 0x0F)):
        s = SNES()
        s.module('snes')
        s.configure(e, True, 'invincible')
        s.turbo(4)
        s.load(st)
        s.layers(mask)
        runs.append(s)
    ref, on, off = runs
    bad_ram = bad_mid = bad_plain = bad_box = 0
    first_bad = []
    reach = [0, 0]   # px into the left / right side
    side_frames = 0
    gx_last = None
    shots, labels = [], []
    f = 0
    seen = False
    while f < 1500:
        pad = 'B' if f >= 600 and f % 50 < 3 else 0
        for s in runs:
            s.hold(pad)
            s.run(1)
        f += 1
        sc = ref.r8(0x35E)
        if sc == 0x24:
            seen = True
        elif seen and sc < 0x14:
            break
        w0 = ref.wram_bytes()
        if on.wram_bytes() != w0 or off.wram_bytes() != w0:
            bad_ram += 1
            if len(first_bad) < 4:
                first_bad.append(('ram', f))
        gx = genie_x(on)
        if sc == 0x24 and on.info()['gameplay']:
            a = grab_np(on).astype(np.int32)
            b = grab_np(off).astype(np.int32)
            r = grab_np(ref).astype(np.int32)
            vl = on.view_left()
            vl = ext if vl is None else vl
            if not np.array_equal(a[:, vl:vl + 256], r[:, :256]):
                bad_mid += 1
                if len(first_bad) < 4:
                    first_bad.append(('mid', f))
            sides_b = np.concatenate([b[:, :vl].reshape(-1, 3), b[:, vl + 256:].reshape(-1, 3)])
            u = np.unique(sides_b, axis=0)
            if len(u) != 1:
                bad_plain += 1
                if len(first_bad) < 4:
                    first_bad.append(('plain', f))
                continue
            bd = u[0]
            diff = np.any(a != bd, axis=2)
            diff[:, vl:vl + 256] = False
            cols = np.nonzero(diff.any(axis=0))[0] - vl   # screen x of the sides' sprite pixels
            if len(cols):
                side_frames += 1
                ok = np.zeros(len(cols), bool)
                for g in (gx, gx_last):
                    if g is not None:
                        ok |= (cols >= g - 32) & (cols < g + 32)
                if not ok.all():
                    bad_box += 1
                    if len(first_bad) < 4:
                        first_bad.append(('box', f, int(cols.min()), int(cols.max()), gx, gx_last))
                reach[0] = max(reach[0], int(-cols.min()) if cols.min() < 0 else 0)
                reach[1] = max(reach[1], int(cols.max() - 255) if cols.max() > 255 else 0)
                if f % 6 == 0 and len(shots) < 24:
                    shots.append(img(on))
                    labels.append('f%d genie x %s' % (f, gx))
        gx_last = gx
    if shots:
        sheet(shots, os.path.join(QA, 'clear_genie_%d_%02d.png' % (ext, lv)), cols=4, labels=labels)
    return arg, (f, seen, bad_ram, bad_mid, bad_plain, bad_box, side_frames, reach, first_bad)


if __name__ == '__main__':
    exts = [int(e) for e in sys.argv[1].split(',')] if len(sys.argv) > 1 else [30, 46, 68, 106, 140]
    lvs = [int(a) for a in sys.argv[2].split(',')] if len(sys.argv) > 2 else [3, 5, 7, 10, 13, 14, 17]
    jobs = [(lv, e) for lv in lvs for e in exts]
    with ProcessPoolExecutor(min(12, len(jobs))) as ex:
        res = dict(ex.map(job, jobs))
    fails = 0
    for lv in lvs:
        for e in exts:
            f, seen, br, bm, bp, bb, sf, reach, fb = res[(lv, e)]
            fails += br + bm + bp + bb + (0 if seen else 1)
            print('level %2d ext %3d: %d frames%s; RAM differs %d, game screen differs %d, sides not plain without '
                  'sprites %d, sprite pixels off the Genie %d; Genie in the sides on %d frames, up to %d px left / %d '
                  'px right %s' % (lv, e, f, '' if seen else ' (no STAGE CLEAR)', br, bm, bp, bb, sf, reach[0],
                                   reach[1], fb if fb else ''))
    print('FAIL' if fails else 'ok')
