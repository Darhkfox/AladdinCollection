"""A state just before the SNES staff roll (for its wide recording): Jafar's level started headless, the cobra marked
beaten ($1B2B, see docs/re_snes_game.md), and the ending run with no input until the staff roll (level $1C) starts.
  python snes_staff.py probe    shots every 10 s to see what happens
  python snes_staff.py          writes trailer_work/cap/snes_staff__0.state (app format, 16:9 CRT)
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tb import CAP, write_state, ext_of  # noqa: E402
from games import SNESGame  # noqa: E402

ext = ext_of('snes', 2)
g = SNESGame(ext=ext)
assert g.start(18)
g.run(60)
s = g.m
s.w8(0x1B2B, 1)
probe = sys.argv[1:] == ['probe']
for t in range(400):
    g.run(60)
    lvl = s.r8(0x35E)
    if probe and t % 10 == 0:
        s.png(os.path.join(CAP, 'staff_probe_%03d.png' % t), 1)
        print(t, 'level', hex(lvl), 'stage', s.r8(0x35C), s.r8(0x35D), 'cam', s.r16(0x1773), flush=True)
    if lvl == 0x1C and s.r16(0x1773) > 0:
        print('staff roll at', t, 's', flush=True)
        if not probe:
            write_state('snes_staff__0', g.app_state(g.save()))
        break
