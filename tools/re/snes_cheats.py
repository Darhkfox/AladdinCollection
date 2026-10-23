"""Cheat checks: random play in a few levels with each cheat, logging hearts / lives / apples / gems.
python snes_cheats.py [frames]"""
import sys
import os
import random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa

frames = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
MOVES = ['RIGHT', 'RIGHT+B', 'RIGHT+Y', 'RIGHT+A', 'LEFT', 'B', 'A', 'DOWN', 'UP', 'RIGHT+B+A', '']


def play(level, cheats, seed=1):
    s = SNES()
    s.module('snes')
    s.configure(0, True, cheats)
    s.start_level(level)
    r = random.Random(seed)
    lo = {'hearts': 99, 'lives': 99, 'apples': 0x99}
    hurt = 0
    prev_h = s.r8(0x367)
    done = 0
    while done < frames:
        s.hold(r.choice(MOVES))
        n = r.randint(4, 30)
        for _ in range(n):
            s.run(1)
            h = s.r8(0x367)
            if h < prev_h:
                hurt += 1
            prev_h = h
        done += n
        lo['hearts'] = min(lo['hearts'], s.r8(0x367))
        lo['lives'] = min(lo['lives'], s.r8(0x364))
        lo['apples'] = min(lo['apples'], s.r8(0x369))
    return 'hearts lost %d, min hearts %d, min lives %d, min apples %02X, gems %02X, level %02X' % (
        hurt, lo['hearts'], lo['lives'], lo['apples'], s.r8(0x36B), s.r8(0x35E))


for cheats in ['', 'invincible', 'inf_health', 'inf_lives', 'inf_apples', 'inf_gems', 'invincible,inf_lives']:
    for lv in (0, 11):
        print('%-22s lvl %2d: %s' % (cheats or '(none)', lv, play(lv, cheats)))
