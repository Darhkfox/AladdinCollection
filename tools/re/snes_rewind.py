"""Rewind check: python snes_rewind.py <ext> [levels] [frames] [state]

As the app's rewind does: a state every 2 frames while a bot plays (invincible), then going back: each state loaded,
one frame run (with the input that frame had), its picture. Every rewound picture must equal the straight run's
picture of the same frame, and the module's report (gameplay, the view's split) too. Mismatches are listed and the
first few saved side by side to tools/qa/snes/rewind_<ext>_<level>_<frame>.png. `state` (with one level: e.g.
tools/states/snes_end_<level>.state, snes_end_states.py) replaces the level's start."""
import os
import random
import sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa

ext = int(sys.argv[1]) if len(sys.argv) > 1 else 106
levels = [int(a) for a in sys.argv[2].split(',')] if len(sys.argv) > 2 and sys.argv[2] != 'all' else list(range(19))
frames = int(sys.argv[3]) if len(sys.argv) > 3 else 600
start = sys.argv[4] if len(sys.argv) > 4 else None
MOVES = ['RIGHT', 'RIGHT+B', 'RIGHT+Y', 'RIGHT+A', 'LEFT', 'B', 'A', 'DOWN', 'UP', 'RIGHT+B+A', '']

total = 0
for lv in levels:
    s = SNES()
    s.module('snes')
    s.configure(ext, True, 'invincible')
    if start:
        s.load_file(start)
    else:
        s.start_level(lv)
    rnd = random.Random(lv)
    pads, pics, info, states = [], [], [], {}
    cur, left = '', 0
    for f in range(frames):
        if left == 0:
            cur, left = rnd.choice(MOVES), rnd.randint(4, 30)
        left -= 1
        if f % 2 == 0:
            states[f] = s.save()
        s.hold(cur)
        s.run(1)
        pads.append(cur)
        pics.append(grab_np(s))
        info.append((s.info()['gameplay'], s.view_left()))
    bad = []
    for f in sorted(states, reverse=True):
        s.load(states[f])
        s.hold(pads[f])
        s.run(1)
        p = grab_np(s)
        i = (s.info()['gameplay'], s.view_left())
        same = p.shape == pics[f].shape and np.array_equal(p, pics[f])
        if not same or i != info[f]:
            diff = int((p != pics[f]).any(axis=2).sum()) if p.shape == pics[f].shape else -1
            bad.append((f, diff, info[f], i))
            if len(bad) <= 3:
                from PIL import Image
                im = Image.new('RGB', (p.shape[1] + pics[f].shape[1] + 4, max(p.shape[0], pics[f].shape[0])))
                im.paste(Image.fromarray(pics[f]), (0, 0))
                im.paste(Image.fromarray(p), (pics[f].shape[1] + 4, 0))
                im.save(QA + '/rewind_%d_%02d_%d.png' % (ext, lv, f))
    total += len(bad)
    print('level %2d: %d of %d rewound frames differ %s' % (lv, len(bad), len(states),
                                                          ' '.join('f%d:%dpx%s' % (f, d, '' if a == b else ' %s->%s' % (a, b))
                                                                   for f, d, a, b in bad[:8])), flush=True)
print('total', total)
