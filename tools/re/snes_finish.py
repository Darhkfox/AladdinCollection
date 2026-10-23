"""Reach a level's exit quickly (the player put just before the exit of the exit table $80:EC1E) and watch what
follows: python snes_finish.py <level index 0-18> [ext] [frames] [bonus]
With `bonus`, $036C (a bonus token collected) is set first. Writes tools/qa/snes/finish_<n>.png and saves
tools/states/after_<n>.state."""
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa

LEVELS = [(0, 0), (0, 1), (0, 2), (0, 3), (1, 0), (1, 1), (2, 0), (2, 1), (3, 0), (3, 1), (3, 2), (4, 0), (4, 1),
          (4, 2), (6, 0), (5, 0), (5, 1), (5, 2), (7, 0)]


def to_exit(s):
    lvl = s.r8(0x35E)
    off = s.r16(0x80EC1E + lvl * 2)
    ex = s.r16(0x80EC1F + off)
    ey = s.r16(0x80EC1F + off + 2)
    return ex, ey


if __name__ == '__main__':
    n = int(sys.argv[1])
    ext = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    frames = int(sys.argv[3]) if len(sys.argv) > 3 else 1800
    bonus = len(sys.argv) > 4
    s = SNES()
    s.module('snes')
    s.configure(ext, True, 'invincible,inf_lives')
    s.start_level(n)
    s.run(200)
    if bonus:
        s.w8(0x36C, 1)
    ex, ey = to_exit(s)
    print('exit', hex(ex), hex(ey))
    # (the exit moved to where the player stands: ROM poked in this instance only)
    lvl = s.r8(0x35E)
    off = s.r16(0x80EC1E + lvl * 2)
    s.w16(0x80EC1F + off, s.r16(0x08E7))
    s.w16(0x80EC1F + off + 2, s.r16(0x08EA))
    ims, lab = [], []
    s.hold(0)
    for i in range(frames // 60):
        s.run(60)
        ims.append(img(s).resize((256 + 2 * ext, 224)))
        lab.append('f%d st%d-%d lvl%02X gp=%d vl=%s' % (i * 60, s.r8(0x35C), s.r8(0x35D), s.r8(0x35E), s.info()['gameplay'],
                                                         s.view_left()))
        if i == 3:
            s.hold(0)
    s.save_file(STATES + '/after_%d.state' % n)
    sheet(ims, QA + '/finish_%d.png' % n, cols=6, labels=lab)
