"""The module's level select for every level: python snes_levels.py [ext] [after]
Starts each level from power-on (start_level), saves tools/states/lvl_NN.state (as it starts) and a sheet of pictures
`after` frames later (default 90)."""
import sys
import os
import time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa

ext = int(sys.argv[1]) if len(sys.argv) > 1 else 0
after = int(sys.argv[2]) if len(sys.argv) > 2 else 90
ims, lab = [], []
for i in range(19):
    s = SNES()
    s.module('snes')
    s.configure(ext)
    t = time.time()
    ok = s.start_level(i)
    dt = time.time() - t
    f0 = s.frame
    s.save_file(STATES + '/lvl_%02d.state' % i)
    s.run(after)
    ims.append(img(s).resize((256 + 2 * ext, 224)))
    lab.append('%d ok=%d f=%d st%d-%d lvl%02X %.1fs' % (i, ok, f0, s.r8(0x35C), s.r8(0x35D), s.r8(0x35E), dt))
    print(lab[-1])
sheet(ims, QA + '/levels_%d.png' % ext, cols=5, labels=lab)
