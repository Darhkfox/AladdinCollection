"""Enter every (stage, act) pair from GAME START by poking $035C / $035D when the game first computes the level
index ($81:8157), and take a picture of each level a while after it starts (exploration)."""
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa

wait = int(sys.argv[1]) if len(sys.argv) > 1 else 400
pairs = []
s = SNES()
for st in range(9):
    lo = s.r8(0x80B73E + st)
    hi = s.r8(0x80B73E + st + 1) if st < 8 else lo + 9
    for act in range(hi - lo):
        pairs.append((st, act, s.r8(0x80B73E + lo + act)))
ims, lab = [], []
for st, act, lvl in pairs:
    s = SNES()
    s.load_file(STATES + '/title_idle.state')
    s.run(5)
    s.hold('START')
    ok = s.run_to(0x818157, 3000000)
    s.hold(0)
    s.w8(0x7E035C, st)
    s.w8(0x7E035D, act)
    s.run(wait)
    ims.append(img(s).resize((256, 224)))
    lab.append('%d-%d lvl %02X' % (st, act, lvl))
    s.save_file(STATES + '/scan_%d_%d.state' % (st, act))
    print(st, act, hex(lvl), s.r8(0x35E), ok)
sheet(ims, QA + '/levels_scan.png', cols=6, labels=lab)
