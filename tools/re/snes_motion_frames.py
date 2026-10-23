"""Pictures of the motion check's run (snes_motion.py's input) at 4:3 and at a width, for frames f0..f1 of a level, to
look at what a reported mismatch is: python snes_motion_frames.py <ext> <level> <f0> <f1> [step]"""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa
from PIL import Image

ext, lv, f0, f1 = (int(a) for a in sys.argv[1:5])
step = int(sys.argv[5]) if len(sys.argv) > 5 else 1


def script(f):
    if f < 60:
        return 0
    if f < 300:
        return 'RIGHT'
    if f < 540:
        return 'LEFT'
    return 'RIGHT+B' if (f // 30) % 3 == 0 else 'RIGHT'


runs = []
for e in (0, ext):
    s = SNES()
    s.module('snes')
    s.configure(e, True, 'invincible')
    s.start_level(lv)
    runs.append(s)
ims, lab = [], []
for f in range(f1 + 1):
    for s in runs:
        s.hold(script(f))
        s.run(1)
    if f >= f0 and (f - f0) % step == 0:
        for k, s in enumerate(runs):
            ims.append(img(s))
            lab.append('%s f%d cam %d,%d bg2 %d vl=%s' % ('4:3' if k == 0 else 'wide', f, s.r16(0x1773), s.r16(0x1777),
                                                        s.ppu_regs()['bg2']['hofs'], s.view_left()))
out = QA + '/motion_frames_%d_%d_%d.png' % (ext, lv, f0)
sheet(ims, out, cols=2, labels=lab)
print(out)
