"""Enter a level from the title the way the game's own debug stage select does (stage / act set when GAME START
computes the level, $81:8157), with the module configured: python snes_enter.py <stage> <act> <ext> [frames]
Saves tools/states/enter_<stage>_<act>_<ext>.state and a picture."""
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa


def enter(stage, act, ext, frames=300, cheats=''):
    s = SNES()
    s.module('snes')
    s.configure(ext, True, cheats)
    s.load_file(STATES + '/title_idle.state')
    s.run(5)
    s.hold('START')
    s.run_to(0x818157, 3000000)
    s.hold(0)
    s.w8(0x7E035C, stage)
    s.w8(0x7E035D, act)
    s.run(frames)
    return s


if __name__ == '__main__':
    st, act, ext = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
    fr = int(sys.argv[4]) if len(sys.argv) > 4 else 300
    s = enter(st, act, ext, fr)
    s.save_file(STATES + '/enter_%d_%d_%d.state' % (st, act, ext))
    img(s).save(QA + '/enter_%d_%d_%d.png' % (st, act, ext))
    print(s.info(), s.view_left(), 'cam', s.r16(0x1773), s.r16(0x1777))
