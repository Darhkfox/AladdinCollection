"""Layer motion in widescreen against the original:
python snes_motion.py <ext> [levels] [frames] [start] [hover]

Four emulators run each level in lockstep with the same input (invincible; a walk right, back left, then right with
jumps): BG1 alone and BG2 alone at 4:3 and at the width. Per frame each layer's horizontal motion on screen is
measured from the pixels (the shift that best maps the last picture onto this one, lines below the status bar):
d1 / d2 at 4:3, w1 / w2 in widescreen, and the view's split change del (FrameInfo::view_left). `start` (a 4:3 state,
with `levels` its level: tools/states/snes_end_<level>.state from snes_end_states.py, some 120 px before a level's
right end) replaces the level's start: the walk is then right to the end, back and right again; `hover` holds Aladdin
in the air on the screen (the Lava Cavern, an auto-scroller). A run stops when its level is over.

A layer must move in the wide picture as the view moves over the world: BG1 by d1 + del, a background at speed r by
d2 + r * del (it scrolls as if the camera were at the picture's middle; r from $083F). Where the game scrolls the
background itself ($083F $0A-$14: raster bands, a phase, an object), each band is checked on its own lines from the
wide run's registers (snt.py line_regs: the game's own scroll and the module's shift), against the game's own move of
it in that run (so the runs drifting apart doesn't count): its speed, or, for mode $0A's top four bands, which hold
still while the camera moves right and move with it (1, 1/2, 1/4, 1/8) while it moves left, d2 - (min(0, dv) -
min(0, dc)) / 2^k for the camera's move dc and the view's dv = dc - del; the pixels of those bands where the view moves
are listed too (a drifting or animated band - the lava - can measure wrong there). Listed:
  still : the view moves (del != 0) while the game's own screen holds still (BG1 in it: g1 = 0)
  bg1   : BG1 doesn't move with the view (w1 != g1 + del; g1 measured in the wide picture's game screen, so a frame
          of lag between the runs doesn't count)
  bg2   : BG2 (or a band of it) doesn't (|w2 - expected| > 1)
A camera jump (a life lost, a restart) and the frame after it are skipped.
Writes tools/qa/snes/motion_<ext>.txt (motion_<ext>_<state>.txt from a state)."""
import os
import sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa

ext = int(sys.argv[1]) if len(sys.argv) > 1 else 106
levels = [int(a) for a in sys.argv[2].split(',')] if len(sys.argv) > 2 and sys.argv[2] != 'all' else list(range(19))
frames = int(sys.argv[3]) if len(sys.argv) > 3 else 900
start = sys.argv[4] if len(sys.argv) > 4 and sys.argv[4] != '-' else None
hover = len(sys.argv) > 5 and sys.argv[5] == 'hover'   # (an auto-scroller: Aladdin held in the air on the screen)
RATIO = {0: 0.5, 6: 0.5, 8: 0.5, 2: 1.0, 4: 1.0, 0x12: 1.0, 0x14: 1.0}
ROW0 = 11   # an HDMA table's second entry's first row (the IRQ at line 8 switches the channel on)


def make(e, mask):
    s = SNES()
    s.module('snes')
    s.configure(e, True, 'invincible')
    s.layers(mask)
    return s


def shift(a, b, maxd=10, rows=None):
    """The dx with b[x] ~ a[x - dx] (content moved right by dx), on lines 48..208 (or `rows`), by the least mean
    difference; None for a flat picture."""
    if a is None:
        return 0
    sel = slice(48, 208) if rows is None else rows
    A = a[sel].astype(np.int32).sum(axis=2)
    B = b[sel].astype(np.int32).sum(axis=2)
    if A.size == 0:
        return None
    w = A.shape[1]
    best, bd = 1e18, 0
    for d in range(-maxd, maxd + 1):
        if d >= 0:
            diff = np.abs(B[:, d:] - A[:, :w - d]).mean()
        else:
            diff = np.abs(B[:, :w + d] - A[:, -d:]).mean()
        if diff < best:
            best, bd = diff, d
    flat = A.std() < 1.0
    return None if flat else bd


def bands(s, mode, sub):
    """Per band: (rows of the game's screen it covers this frame, kind, factor). kind 'r': speed `factor` against the
    camera; 'k': mode $0A's ratchet, scaled by 1 / 2^factor. From the game's tables, as its band routines build the
    HDMA tables ($81:C84B, $81:C7E7)."""
    out = {}
    if mode in (0x0A, 0x0E):
        idx = 2 if mode == 0x0A else 0
        n = s.r16(0x80C602 + idx) >> 1
        base = 0x80C606 + s.r16(0x80C606 + idx)
        b = [s.r16(base + 2 * k) for k in range(n + 1)]
        camy = s.r16(0x1777)
        if mode == 0x0A:
            kinds = [('k', 0), ('k', 1), ('k', 2), ('k', 3), ('r', 0.5), ('r', 1.25)]
        else:
            kinds = [('r', 0.5), ('r', 0.25)] + [('r', 0.5)] * (n - 2)
        for r in range(48, 208):
            y = camy + 8 + max(0, r - ROW0)
            k = 0
            while k + 1 < n and y >= b[k + 1]:
                k += 1
            out.setdefault(k, []).append(r)
        return [(rows, kinds[k][0], kinds[k][1]) for k, rows in sorted(out.items())]
    if mode == 0x0C:   # the Lava Cavern: every band drifts on its own but band 9, which moves with the camera
        return [(rows, 'r', 1.0 if k == 9 else 0.0) for k, rows in sorted(lava_rows(s).items())]
    if mode == 0x10:
        cam = s.r16(0x1773)
        return [(list(range(48, 208)), 'r', 0.5 if sub and cam >= 0x1000 else 1.0)]
    return None


def lava_rows(s):
    """The Lava Cavern's table: band per row (entry 0 up to ROW0, then by the entries' line counts)."""
    rows = {}
    e = 1
    cnts = []
    while e < 16:
        c = s.r8(0x7EF400 + 3 * e) & 0x7F
        if c == 0:
            break
        cnts.append(c)
        e += 1
    for r in range(48, 208):
        if r < ROW0:
            k = 0
        else:
            k, acc = len(cnts), ROW0
            for i, c in enumerate(cnts):
                if r < acc + c:
                    k = i + 1
                    break
                acc += c
            k = min(k, len(cnts))
        rows.setdefault(k, []).append(r)
    return rows


def expect(kind, fac, d2, de, dc):
    """A band's expected move in the wide picture: its move at 4:3 (d2) for a camera move dc, the view's move
    dc - de instead: at speed fac, d2 + fac * de; mode $0A's ratchet (1 / 2^fac), d2 - (min(0, dv) - min(0, dc))."""
    if kind == 'r':
        return d2 + fac * de
    dv = dc - de
    return d2 - (min(0, dv) - min(0, dc)) / (1 << fac)


def rows_name(rows):
    return 'rows %d-%d' % (rows[0], rows[-1]) if rows else 'all'


def script(f):
    if start:   # (from a state some 400 px before a level's right end: to the end, back, and to it again)
        if f < 30:
            return 0
        if f < 150:
            return 'RIGHT'
        if f < 450:
            return 'LEFT'
        return 'RIGHT+B' if (f // 30) % 3 == 0 else 'RIGHT'
    if f < 60:
        return 0
    if f < 300:
        return 'RIGHT'
    if f < 540:
        return 'LEFT'
    return 'RIGHT+B' if (f // 30) % 3 == 0 else 'RIGHT'


out = []
for lv in levels:
    em = {k: make(e, m) for k, (e, m) in {'n2': (0, 2), 'w1': (ext, 1), 'w2': (ext, 2)}.items()}
    for s in em.values():
        if start:
            s.load_file(start)
        else:
            s.start_level(lv)
    prev = {k: None for k in em}
    prev_el = None
    prev_cam = None
    prev_regs = None
    prev_camw = None
    skip = 0
    pixel_notes = []
    bad = {'still': 0, 'bg1': 0, 'bg2': 0}
    notes = []
    mode = em['w1'].r16(0x83F)
    r = RATIO.get(mode)
    level = em['w1'].r8(0x35E)
    for f in range(frames):
        if any(s.r8(0x35E) != level for s in em.values()):
            notes.append('f%d: the level is over' % f)
            break
        for s in em.values():
            if hover:
                s.w16(0x8E7, s.r16(0x1773) + 128)
                s.w16(0x8EA, s.r16(0x1777) + 100)
            s.hold(script(f) if not hover else 0)
            s.run(1)
        pics = {k: grab_np(s) for k, s in em.items()}
        el = em['w1'].view_left()
        cam = em['n2'].r16(0x1773)
        regs = {'n2': em['n2'].line_regs(), 'w2': em['w2'].line_regs()}
        camw = em['w1'].r16(0x1773)
        # (a jump of the camera - a life lost, a restart - and the frame after it: not motion)
        skip = max(0, skip - 1)
        if prev_cam is not None and (abs(cam - prev_cam) >= 64 or abs(camw - prev_camw) >= 64):
            skip = 2
        live = prev['w1'] is not None and el is not None and prev_el is not None and em['w1'].info()['gameplay']
        if live and not skip:
            g1 = shift(prev['w1'][:, prev_el:prev_el + 256], pics['w1'][:, el:el + 256])
            w1 = shift(prev['w1'], pics['w1'])
            de = el - prev_el
            if None not in (g1, w1):
                if de != 0 and g1 == 0:
                    bad['still'] += 1
                    notes.append('f%d still: game screen %d, view %d, picture %s' % (f, g1, de, w1))
                elif w1 != g1 + de:
                    bad['bg1'] += 1
                    notes.append('f%d bg1: g1 %d del %d w1 %d' % (f, g1, de, w1))
            dc = cam - prev_cam if prev_cam is not None else 0
            if mode >= 0x0A and prev_regs is not None:
                # The game scrolls it itself (bands, drift, an animated picture): from the wide run's own registers,
                # each band against the game's own move of it (so the runs drifting apart doesn't count), and the
                # pixels where the view moves (a drifting or animated band can measure wrong there: listed only).
                dcw = camw - prev_camw
                wrap = lambda x: ((x + 512) & 1023) - 512
                for rows, kind, fac in bands(em['w2'], mode, em['w2'].r16(0x841)) or [(None, 'r', r)]:
                    if rows is not None and len(rows) < 6:
                        continue
                    row = rows[len(rows) // 2] if rows else 128
                    now, last = regs['w2'][row], prev_regs['w2'][row]
                    own = -wrap(now[2] - last[2])
                    w2 = -wrap(now[2] + now[5] - last[2] - last[5]) + de
                    exp = expect(kind, fac, own, de, dcw)
                    if abs(w2 - exp) > 1:
                        bad['bg2'] += 1
                        notes.append('f%d bg2 %s %s%s (registers): own %d del %d dc %d w2 %d (expected %.2f)'
                                     % (f, rows_name(rows), kind, fac, own, de, dcw, w2, exp))
                if de != 0:
                    for rows, kind, fac in bands(em['n2'], mode, em['n2'].r16(0x841)) or [(None, 'r', r)]:
                        if rows is not None and len(rows) < 6:
                            continue
                        d2 = shift(prev['n2'], pics['n2'], rows=rows)
                        w2 = shift(prev['w2'], pics['w2'], rows=rows)
                        if None not in (d2, w2) and abs(w2 - expect(kind, fac, d2, de, dc)) > 1:
                            pixel_notes.append('f%d %s %s%s: d2 %d del %d w2 %d (expected %.2f)'
                                               % (f, rows_name(rows), kind, fac, d2, de, w2,
                                                  expect(kind, fac, d2, de, dc)))
            elif mode < 0x0A:
                d2 = shift(prev['n2'], pics['n2'])
                w2 = shift(prev['w2'], pics['w2'])
                if r is not None and None not in (d2, w2) and abs(w2 - (d2 + r * de)) > 1:
                    bad['bg2'] += 1
                    notes.append('f%d bg2: d2 %d del %d w2 %d (r %.2f)' % (f, d2, de, w2, r))
                elif r is None and None not in (d2, w2) and de != 0 and w2 != d2:
                    bad['bg2'] += 1
                    notes.append('f%d bg2 (mode %02X, unmodelled): d2 %d del %d w2 %d' % (f, mode, d2, de, w2))
        prev_regs = regs
        prev = pics
        prev_el = el
        prev_cam = cam
        prev_camw = camw
    line = 'level %2d mode %02X: still %d, bg1 %d, bg2 %d' % (lv, mode, bad['still'], bad['bg1'], bad['bg2'])
    if mode >= 0x0A:
        line += ' (pixels of a moving view off by more than 1 px: %d)' % len(pixel_notes)
    print(line, flush=True)
    out.append(line)
    out += ['   ' + n for n in notes[:40]]
    out += ['   pixels ' + n for n in pixel_notes[:20]]
name = '/motion_%d.txt' % ext if not start else '/motion_%d_%s.txt' % (ext, os.path.splitext(os.path.basename(start))[0])
open(QA + name, 'w').write('\n'.join(out) + '\n')
