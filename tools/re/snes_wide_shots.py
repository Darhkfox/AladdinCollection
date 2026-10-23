"""Widescreen pictures of level states: python snes_wide_shots.py <ext> [hold] [frames] [state ...]

Loads each state (default: every scan_*.state), configures the module at the width, runs a few frames, then holds a
direction for `frames` frames (default RIGHT, 120), and writes a sheet of the start and the end to tools/qa/snes."""
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa

ext = int(sys.argv[1]) if len(sys.argv) > 1 else 106
hold = sys.argv[2] if len(sys.argv) > 2 else 'RIGHT'
frames = int(sys.argv[3]) if len(sys.argv) > 3 else 120
names = sys.argv[4:] or [f[:-6] for f in sorted(os.listdir(STATES)) if f.startswith('scan_') and not f.startswith('scan_8')]
ims, lab = [], []
for n in names:
    s = SNES()
    s.module('snes')
    s.configure(ext)
    s.load_file(STATES + '/%s.state' % n)
    s.run(4)
    ims.append(img(s))
    lab.append('%s start view_left=%s' % (n, s.view_left()))
    s.hold(hold)
    s.run(frames)
    s.hold(0)
    s.run(2)
    ims.append(img(s))
    lab.append('%s +%d %s view_left=%s cam=%d' % (n, frames, hold, s.view_left(), s.r16(0x1773)))
for k in range(0, len(ims), 8):
    out = QA + '/wide_%d_%s_%d.png' % (ext, hold.replace('+', ''), k // 8)
    sheet(ims[k:k + 8], out, cols=2, labels=lab[k:k + 8])
    print(out)
