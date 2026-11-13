"""High frame rate: the in-between pictures, headless, for the three games (the cores' sub_frames, docs/hfr in the
README). Per console, levels played at the widest width with 4 pictures a frame (3 in-betweens), against a twin run
with the high frame rate off and the same input:

  state   the work RAM and the frame's own picture are identical with and without the in-betweens, every frame
  between each in-between sits between the frames: the picture's shift from the last frame to this one (d, measured
          from the pixels over the playfield) and in-between k's shift to this frame are (1 - k/4) d, within a pixel
  edges   the widescreen sides move with the 4:3 part: the left side, the 4:3 part and the right side of an in-between
          are shifted alike against the frame (no tearing, no drift of the extension)
  cuts    where the picture changes as a whole (a scene cut, a fade, a level start, a death) an in-between is the new
          picture, never a mix with the old one; after a state is loaded the in-betweens are the frame itself
  hud     the status bar (only its layer drawn) is the same in the in-betweens as in the frame wherever it is still

    python tools/re/hfr_test.py [md|snes|sms|all] [--frames N] [--shots DIR] [--perf]

Exits non-zero on a failure. --shots saves sample strips (last frame | in-betweens | frame) as PNG."""
import argparse
import os
import sys
import zlib
import struct
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
K = 4   # pictures a frame (240 Hz)


def to_np(px, w, h):
    a = np.frombuffer(bytes(px) if not isinstance(px, list) else np.array(px, dtype=np.uint32).tobytes(),
                      dtype=np.uint32)
    return a.reshape(h, w).copy()


def png(path, img):
    h, w = img.shape
    rgb = np.stack([(img >> 16) & 255, (img >> 8) & 255, img & 255], axis=2).astype(np.uint8)
    raw = b''.join(b'\0' + rgb[y].tobytes() for y in range(h))
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


# ---- the consoles ------------------------------------------------------------------------------------------------
class Md:
    name, ext, rows, hud = 'md', 128, (24, 184), [(196, 224)]
    hud_layers, scroll_layers = 4, [1, 2]   # (sprites alone: the status bar; plane A, plane B)
    base = 320

    def __init__(self, sub, layers=7):
        from mdt import ModuleMD, ROM
        import md_util
        self.m = ModuleMD(ROM, 'md')
        self.m.configure(self.ext, True, ('invincible',))
        self.m.set_sub_frames(sub)
        self.m.debug_layers(layers)
        self.util = md_util

    def start(self, level):
        if level >= 0:   # (-1: from power-on, the logos, title and attract demos)
            self.util.start_play(self.m, level)
        self.m.set_sub_frames(self.sub_k)

    def step(self, buttons):
        self.m.hold(buttons)
        self.m.mrun(1)

    def pic(self, k):
        w, h, px = self.m.sub_pixels(k)
        return np.array(px, dtype=np.uint32).reshape(h, w)

    def ram(self):
        return bytes(self.m.ram)

    def save(self):
        return self.m.save_full()

    def load(self, st):
        self.m.load_full(st)


class Snes:
    name, ext, rows, hud = 'snes', 106, (48, 208), [(8, 36)]
    hud_layers, scroll_layers = 0x04, [0x01, 0x02]   # (BG3: the status bar; BG1, BG2)
    base = 256

    def __init__(self, sub, layers=0x1f):
        from snt import SNES
        self.m = SNES()
        self.m.module('snes')
        self.m.configure(self.ext, True, 'invincible')
        self.m.set_sub_frames(sub)
        self.m.layers(layers)

    def start(self, level):
        if level < 0:   # (from power-on: the logos, title and attract demos)
            return
        self.m.start_level(level)
        for _ in range(30):
            self.step(0)

    def step(self, buttons):
        self.m.hold(buttons)
        self.m.run(1)

    def pic(self, k):
        w, h, px = self.m.sub_pixels(k)
        return np.frombuffer(bytes(px), dtype=np.uint32).reshape(h, w).copy()

    def ram(self):
        return self.m.wram_bytes()

    def save(self):
        return self.m.save()

    def load(self, st):
        self.m.load(st)


class Sms:
    name, ext, rows, hud = 'sms', 74, (24, 168), [(0, 16)]
    hud_layers, scroll_layers = 1, [1]   # (the background: its status bar; the background)
    base = 256

    def __init__(self, sub, layers=3):
        from smst import SMS
        self.m = SMS()
        self.m.module('sms')
        self.m.configure(self.ext, True, 'invincible')
        self.m.set_ext(self.ext)
        self.m.set_sub_frames(sub)
        self.m.layers(layers)

    def start(self, level):
        if level < 0:   # (from power-on: the logos, title and attract demos)
            return
        self.m.start_level(level)
        for _ in range(30):
            self.step(0)

    def step(self, buttons):
        self.m.mframes(1, buttons)

    def pic(self, k):
        px, w, h = self.m.sub_pixels(k)
        return np.array(px, dtype=np.uint32).reshape(h, w)

    def ram(self):
        return bytes(self.m.ram)

    def save(self):
        return self.m.save()

    def load(self, st):
        self.m.load(st)


# Levels and input per console: (level, frames, input(f)) - walks right with jumps, a turn, the auto-scrollers.
def walk(right, jump, n):
    def f(i):
        if (i // 150) % 4 == 3:
            return 'LEFT'
        return ('RIGHT+' + jump) if (i % 50) < 8 else 'RIGHT'
    return f


SCENES = {
    'md': [(-1, 1500, lambda i: 0), (0, 600, walk('RIGHT', 'C', 0)), (4, 400, walk('RIGHT', 'C', 0)), (8, 500, lambda i: 'UP' if i % 80 < 30 else 0),
           (2, 400, walk('RIGHT', 'C', 0))],
    'snes': [(-1, 1500, lambda i: 0), (0, 600, walk('RIGHT', 'B', 0)),
             (3, 400, walk('RIGHT', 'B', 0)), (6, 400, walk('RIGHT', 'B', 0))],
    'sms': [(-1, 1500, lambda i: 0), (0, 600, walk('RIGHT', '1', 0)), (1, 400, walk('RIGHT', '1', 0)), (5, 400, walk('RIGHT', '1', 0)),
            (7, 400, lambda i: 'UP' if i % 90 < 30 else 'DOWN' if i % 90 < 50 else 0)],
}


# ---- measuring --------------------------------------------------------------------------------------------------
def lum(img):
    """luminance, normalised over the picture (a fade, which scales the colours, measures as the same picture)"""
    v = ((((img >> 16) & 255) * 3 + ((img >> 8) & 255) * 6 + (img & 255)) / 10.0)
    sd = v.std()
    return (v - v.mean()) * (40.0 / sd) if sd > 1 else v * 0


def shift(a, b, rows, x0, x1, maxd=12, center=0):
    """(dx, err, flat): b[x] ~ a[x - dx] over the rows and columns [x0, x1), the mean difference at the best dx in
    center +- maxd (on the normalised luminance: a typical pixel is 40 off the mean)."""
    A = lum(a[rows[0]:rows[1]])
    B = lum(b[rows[0]:rows[1]])
    if B[:, x0:x1].std() < 4:
        return 0, 0.0, True
    best, bd = 1e18, 0
    w = A.shape[1]
    for d in range(center - maxd, center + maxd + 1):
        lo, hi = max(x0, d), min(x1, w + d)
        if hi - lo < 16:
            continue
        e = np.abs(B[:, lo:hi] - A[:, lo - d:hi - d]).mean()
        if e < best - 1e-9 or (abs(e - best) < 1e-9 and abs(d) < abs(bd)):
            best, bd = e, d
    return bd, best, False


def err2(a, b, rows, x0, x1, maxd):
    """the least mean difference of b against a moved by up to maxd px either way, in x and y (a cut's test: a
    picture moving in y measures as itself)"""
    A, B = lum(a), lum(b)
    r0, r1 = rows[0] + maxd, rows[1] - maxd
    best = 1e18
    for dy in range(-maxd, maxd + 1):
        for dx in range(-maxd, maxd + 1):
            lo, hi = max(x0, dx), min(x1, B.shape[1] + dx)
            e = np.abs(B[r0:r1, lo:hi] - A[r0 - dy:r1 - dy, lo - dx:hi - dx]).mean()
            best = min(best, e)
    return best


def bands(rows, n=16):
    return [(r, min(r + n, rows[1])) for r in range(rows[0], rows[1], n) if min(r + n, rows[1]) - r >= 8]


def run_console(C, frames_cap, shots):
    """Plays each scene with five machines on the same input: the in-betweens on (all layers), off (the reference),
    the status bar's layer alone, and each scrolling layer alone (the motion is measured layer by layer and in bands
    of 16 lines, where it is one scroll)."""
    fails = []
    for level, frames, inp in SCENES[C.name]:
        frames = min(frames, frames_cap)
        a, b = C(K), C(1)
        hud_a = C(K, layers=C.hud_layers)
        lay = [C(K, layers=m) for m in C.scroll_layers]
        for e in [a, b, hud_a] + lay:
            e.sub_k = K if e is not b else 1
            e.start(level)
        prev = prev_hud = None
        prev_lay = [None] * len(lay)
        n = dict.fromkeys(['between', 'between_ok', 'edges', 'edges_ok', 'cut', 'cut_ok', 'hud', 'hud_ok'], 0)
        n_state = 0
        shot_done = 0
        bad = []

        def note(s):
            if len(bad) < 12:
                bad.append(s)
        for f in range(frames):
            btn = inp(f)
            for e in [a, b, hud_a] + lay:
                e.step(btn)
            # state: RAM and the frame's picture
            pa, pb = a.pic(0), b.pic(0)
            n_state += 1
            if a.ram() != b.ram() or pa.shape != pb.shape or not np.array_equal(pa, pb):
                fails.append('%s level %d frame %d: state or picture differs with the in-betweens' % (C.name, level, f))
                break
            subs = [a.pic(k) for k in range(1, K)]
            W = pa.shape[1]
            E = (W - C.base) // 2
            if prev is not None and prev.shape == pa.shape:
                d, err, flat = shift(prev, pa, C.rows, E, W - E)
                still = shift(prev, pa, C.rows, E, W - E, 0)[1]
                if not flat and still > 20 and err > 12 and err2(prev, pa, C.rows, E, W - E, 12) > 12:
                    # a cut: the picture changed as a whole - each in-between is the new picture, not the old one
                    for s in subs:
                        n['cut'] += 1
                        e_new = err2(s, pa, C.rows, E, W - E, 4)
                        e_old = err2(s, prev, C.rows, E, W - E, 4)
                        if e_new <= e_old * 0.5 + 1:
                            n['cut_ok'] += 1
                        else:
                            note('cut f%d: in-between %.1f from the frame, %.1f from the last' % (f, e_new, e_old))
                if shots and shot_done < 2 and not flat and err < 8 and abs(d) >= 2 and f > 60 + 150 * shot_done:
                    gap = np.full((pa.shape[0], 4), 0xFFFFFF, np.uint32)
                    parts = [prev]
                    for s in subs + [pa]:
                        parts += [gap, s]
                    png(os.path.join(shots, '%s_level%d_f%d.png' % (C.name, level, f)), np.concatenate(parts, axis=1))
                    shot_done += 1
            # between and edges, layer by layer, band by band
            for li, e in enumerate(lay):
                p = e.pic(0)
                q = prev_lay[li]
                prev_lay[li] = p
                if q is None or q.shape != p.shape:
                    continue
                es = [e.pic(k) for k in range(1, K)]
                for rb in bands(C.rows):
                    d, err, flat = shift(q, p, rb, E, W - E)
                    if flat or err > 6 or d == 0:
                        continue
                    for k, s in enumerate(es, 1):
                        sd, serr, _ = shift(s, p, rb, E, W - E)
                        want = (1 - k / K) * d
                        n['between'] += 1
                        if abs(sd - want) <= 1.01:
                            n['between_ok'] += 1
                        else:
                            note('between f%d layer %d lines %d-%d k%d: d %d, in-between %d' % (f, li, rb[0], rb[1], k, d, sd))
                        if E < 24:
                            continue
                        for side, (x0, x1) in (('left', (8, E - 4)), ('right', (W - E + 4, W - 8))):
                            xd, xe, xf = shift(s, p, rb, x0, x1)
                            if xf or xe > 6:
                                continue
                            n['edges'] += 1
                            # (the 4:3 part's shift fits the side as well as its own best: a repeating texture can measure
                            # a period off)
                            fit = min(shift(s, p, rb, x0, x1, 0, dd)[1] for dd in (sd - 1, sd, sd + 1))
                            if abs(xd - sd) <= 1 or fit <= xe + 0.5:
                                n['edges_ok'] += 1
                            else:
                                note('edge f%d layer %d lines %d-%d k%d %s: %d vs the 4:3 part %d' % (f, li, rb[0], rb[1], k, side, xd, sd))
            # the status bar: still in the in-betweens wherever it is still between the frames
            hp = hud_a.pic(0)
            if prev_hud is not None and prev_hud.shape == hp.shape:
                hsubs = [hud_a.pic(k) for k in range(1, K)]
                for r0, r1 in C.hud:
                    if np.array_equal(prev_hud[r0:r1], hp[r0:r1]):
                        for s in hsubs:
                            n['hud'] += 1
                            if np.array_equal(s[r0:r1], hp[r0:r1]):
                                n['hud_ok'] += 1
                            else:
                                note('hud f%d lines %d-%d moved' % (f, r0, r1))
            prev, prev_hud = pa, hp
        # a state loaded: nothing in between with what was shown before
        st = a.save()
        for _ in range(5):
            a.step('RIGHT')
        a.load(st)
        a.step('RIGHT')
        loaded_ok = all(np.array_equal(a.pic(k), a.pic(0)) for k in range(1, K))
        if not loaded_ok:
            fails.append('%s level %d: in-betweens after a state load are not the frame' % (C.name, level))
        print('%s level %2d: %d frames, same state and picture; between %d/%d, edges %d/%d, cuts %d/%d, hud %d/%d, '
              'load %s' % (C.name, level, n_state, n['between_ok'], n['between'], n['edges_ok'], n['edges'],
                           n['cut_ok'], n['cut'], n['hud_ok'], n['hud'], 'ok' if loaded_ok else 'FAIL'), flush=True)
        for s in bad:
            print('    ' + s)
        rate = lambda ok, t: ok / t if t else 1.0
        if rate(n['between_ok'], n['between']) < 0.95 or (n['between'] < 20 and level >= 0):   # (-1: the attract mode)
            fails.append('%s level %d: in-betweens between the frames %d/%d' % (C.name, level, n['between_ok'], n['between']))
        if rate(n['edges_ok'], n['edges']) < 0.98:
            fails.append('%s level %d: sides shifted unlike the 4:3 part %d/%d' % (C.name, level, n['edges'] - n['edges_ok'], n['edges']))
        if n['cut_ok'] < n['cut']:
            fails.append('%s level %d: cuts mixed %d/%d' % (C.name, level, n['cut'] - n['cut_ok'], n['cut']))
        if rate(n['hud_ok'], n['hud']) < 0.99:
            fails.append('%s level %d: status bar moved %d/%d' % (C.name, level, n['hud'] - n['hud_ok'], n['hud']))
    return fails


def perf(C, frames=600):
    """The core's time per game frame (emulation and drawing, no picture conversion) in the first scene at the widest
    width, with the in-betweens off and with 3 of them (240 Hz)."""
    import time
    level, _, inp = SCENES[C.name][1]   # (the first level)
    out = {}
    for sub in (1, K):
        e = C(sub)
        e.sub_k = sub
        e.start(level)
        for f in range(60):
            e.step(inp(f))
        t = time.perf_counter()
        for f in range(60, 60 + frames):
            e.step(inp(f))
        out[sub] = (time.perf_counter() - t) * 1000 / frames
    print('%s perf at %d px a side: %.2f ms a frame without in-betweens, %.2f ms with %d (+%.2f ms)'
          % (C.name, C.ext, out[1], out[K], K - 1, out[K] - out[1]), flush=True)
    return out


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('console', nargs='?', default='all')
    ap.add_argument('--frames', type=int, default=100000)
    ap.add_argument('--shots', default='')
    ap.add_argument('--perf', action='store_true', help='the cores time per frame, with and without')
    args = ap.parse_args()
    if args.shots:
        os.makedirs(args.shots, exist_ok=True)
    consoles = {'md': Md, 'snes': Snes, 'sms': Sms}
    todo = list(consoles) if args.console == 'all' else args.console.split(',')
    all_fails = []
    if args.perf:
        for c in todo:
            perf(consoles[c])
        sys.exit(0)
    for c in todo:
        f = run_console(consoles[c], args.frames, args.shots)
        all_fails += f
    print('\n'.join(['FAIL: ' + x for x in all_fails]) if all_fails else 'all passed')
    sys.exit(1 if all_fails else 0)
