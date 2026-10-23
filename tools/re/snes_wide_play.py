"""Play levels with a simple bot (invincible, infinite lives) in widescreen and take pictures along the way:
python snes_wide_play.py <ext> <levels, e.g. 0-18 or 3,5> [frames] [every]
Writes tools/qa/snes/play_<ext>_<level>.png. The bot runs right, jumps every so often and throws apples; when stuck
(the camera doesn't move) it tries a long jump, then left."""
import sys
import os
import random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa


def parse(spec):
    out = []
    for part in spec.split(','):
        if '-' in part:
            a, b = part.split('-')
            out += list(range(int(a), int(b) + 1))
        else:
            out.append(int(part))
    return out


def bot(s, frames, every, rnd, shots, labels, tag, cheats='invincible,inf_lives'):
    last_cam = -1
    stuck = 0
    f = 0
    while f < frames:
        cam = s.r16(0x1773)
        stuck = stuck + 1 if cam == last_cam else 0
        last_cam = cam
        if stuck > 40 and rnd.random() < 0.5:
            seq = [('RIGHT+B', 25), ('RIGHT', 10)] if rnd.random() < 0.7 else [('LEFT+B', 20), ('LEFT', 20)]
        else:
            seq = [('RIGHT', rnd.randint(8, 30)), ('RIGHT+B', rnd.randint(6, 20))]
            if rnd.random() < 0.3:
                seq.append(('RIGHT+A', 4))
            if rnd.random() < 0.15:
                seq.append(('UP', 20))
        for b, n in seq:
            s.hold(b)
            for _ in range(n):
                s.run(1)
                f += 1
                if f % every == 0:
                    shots.append(img(s))
                    labels.append('%s f%d cam %d,%d vl=%s lvl %02X' % (tag, f, s.r16(0x1773), s.r16(0x1777),
                                                                      s.view_left(), s.r8(0x35E)))
    s.hold(0)


if __name__ == '__main__':
    ext = int(sys.argv[1])
    levels = parse(sys.argv[2])
    frames = int(sys.argv[3]) if len(sys.argv) > 3 else 1800
    every = int(sys.argv[4]) if len(sys.argv) > 4 else 150
    for lv in levels:
        s = SNES()
        s.module('snes')
        s.configure(ext, True, 'invincible,inf_lives')
        s.start_level(lv)
        shots, labels = [], []
        bot(s, frames, every, random.Random(lv), shots, labels, 'L%d' % lv)
        out = QA + '/play_%d_%02d.png' % (ext, lv)
        sheet(shots, out, cols=3, labels=labels)
        print(out)
