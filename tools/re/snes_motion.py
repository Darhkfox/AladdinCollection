"""Layer motion in widescreen against the original: python snes_motion.py <ext> [levels] [frames]

Four emulators run each level in lockstep with the same input (invincible; a walk right, back left, then right with
jumps): BG1 alone and BG2 alone at 4:3 and at the width. Per frame each layer's horizontal motion on screen is
measured from the pixels (the shift that best maps the last picture onto this one, lines below the status bar):
d1 / d2 at 4:3, w1 / w2 in widescreen, and the view's split change del (FrameInfo::view_left).

A layer must move in the wide picture as the view moves over the world: BG1 by d1 + del, a background at speed r by
d2 + r * del (it scrolls as if the camera were at the picture's middle; r from $083F). Listed:
  still : the view moves (del != 0) while the game's own screen holds still (BG1 in it: g1 = 0)
  bg1   : BG1 doesn't move with the view (w1 != g1 + del; g1 measured in the wide picture's game screen, so a frame
          of lag between the runs doesn't count)
  bg2   : BG2 doesn't (|w2 - (d2 + r * del)| > 1)
Writes tools/qa/snes/motion_<ext>.txt."""
import os
import sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa

ext = int(sys.argv[1]) if len(sys.argv) > 1 else 106
levels = [int(a) for a in sys.argv[2].split(',')] if len(sys.argv) > 2 and sys.argv[2] != 'all' else list(range(19))
frames = int(sys.argv[3]) if len(sys.argv) > 3 else 900
RATIO = {0: 0.5, 6: 0.5, 8: 0.5, 2: 1.0, 4: 1.0}


def make(e, mask):
    s = SNES()
    s.module('snes')
    s.configure(e, True, 'invincible')
    s.layers(mask)
    return s


def shift(a, b, maxd=10):
    """The dx with b[x] ~ a[x - dx] (content moved right by dx), on lines 48..208, by the least mean difference."""
    if a is None:
        return 0
    A = a[48:208].astype(np.int32).sum(axis=2)
    B = b[48:208].astype(np.int32).sum(axis=2)
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


def script(f):
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
        s.start_level(lv)
    prev = {k: None for k in em}
    prev_el = None
    bad = {'still': 0, 'bg1': 0, 'bg2': 0}
    notes = []
    mode = em['w1'].r8(0x83F)
    r = RATIO.get(mode)
    for f in range(frames):
        for s in em.values():
            s.hold(script(f))
            s.run(1)
        pics = {k: grab_np(s) for k, s in em.items()}
        el = em['w1'].view_left()
        if prev['w1'] is not None and el is not None and prev_el is not None and em['w1'].info()['gameplay']:
            g1 = shift(prev['w1'][:, prev_el:prev_el + 256], pics['w1'][:, el:el + 256])
            d2 = shift(prev['n2'], pics['n2'])
            w1 = shift(prev['w1'], pics['w1'])
            w2 = shift(prev['w2'], pics['w2'])
            de = el - prev_el
            if None not in (g1, w1):
                if de != 0 and g1 == 0:
                    bad['still'] += 1
                    notes.append('f%d still: game screen %d, view %d, picture %s' % (f, g1, de, w1))
                elif w1 != g1 + de:
                    bad['bg1'] += 1
                    notes.append('f%d bg1: g1 %d del %d w1 %d' % (f, g1, de, w1))
            if r is not None and None not in (d2, w2) and abs(w2 - (d2 + r * de)) > 1:
                bad['bg2'] += 1
                notes.append('f%d bg2: d2 %d del %d w2 %d (r %.2f)' % (f, d2, de, w2, r))
            elif r is None and None not in (d2, w2) and de != 0 and w2 != d2:
                bad['bg2'] += 1
                notes.append('f%d bg2 (mode %02X, unmodelled): d2 %d del %d w2 %d' % (f, mode, d2, de, w2))
        prev = pics
        prev_el = el
    line = 'level %2d mode %02X: still %d, bg1 %d, bg2 %d' % (lv, mode, bad['still'], bad['bg1'], bad['bg2'])
    print(line, flush=True)
    out.append(line)
    out += ['   ' + n for n in notes[:40]]
open(QA + '/motion_%d.txt' % ext, 'w').write('\n'.join(out) + '\n')
