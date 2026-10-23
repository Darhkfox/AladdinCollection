"""Achievement scenarios for the module (snt's achievement API: indices earned per frame):
python snes_achievements.py
1. A new game from the title played through every level (each part ended by moving its exit to the player, ROM
   poked in this instance only; Jafar's cobra put at its last hit point), the bonus wheel after the first part:
   expects every story achievement, the wheel, no-continue, untouched (invincibility: no heart lost), stage clean.
2. RAM pokes for hearts, lives and gems; 3. the developer code on OPTIONS with controller 2."""
import functools
import os
import sys

print = functools.partial(print, flush=True)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa

NAMES = ['agrabah', 'cave', 'escape', 'lamp', 'dungeon', 'carpet', 'palace', 'jafar', 'no_continue', 'stage_clean',
         'untouched', 'wheel', 'hearts', 'gems', 'lives', 'jafar_flawless', 'debug']
got = []


def step(s, n=1, pad=0, port=0):
    for _ in range(n):
        if pad is not None:
            s.hold(pad, port)
        s.run(1)
        for i in s.ach():
            if i < len(NAMES):
                got.append((s.frame, NAMES[i], s.r8(0x35E)))
                print('  f%d %s (screen %02X)' % (s.frame, NAMES[i], s.r8(0x35E)))


def exit_here(s):
    lvl = s.r8(0x35E)
    off = s.r16(0x80EC1E + lvl * 2)
    s.w16(0x80EC1F + off, s.r16(0x08E7))
    s.w16(0x80EC1F + off + 2, s.r16(0x08EA))


def new_game():
    s = SNES()
    s.module('snes')
    s.configure(0, True, 'invincible,inf_apples')
    s.ach_reset()
    # power-on through the logos and story to the title, GAME START: START tapped until the level's loop runs
    for f in range(3000):
        sp = s.r16(0x71)
        ret = s.r16(sp + 7) | (s.r8(sp + 9) << 16)
        if ret == 0x8182E9 and s.r8(0x0331) == 0:
            break
        step(s, 1, 'START' if (f // 4) % 2 == 0 else 0)
    return s


print('1. a game played through')
s = new_game()
step(s, 400, 0)
levels_done = 0
for f in range(3000):
    lvl = s.r8(0x35E)
    if lvl < 0x14 and lvl != 0x11:
        step(s, 150, 0)
        if s.r8(0x35E) != lvl:
            continue
        if levels_done == 0:
            s.w8(0x36C, 1)            # a Genie token: the bonus wheel follows
        exit_here(s)
        levels_done += 1
        for k in range(400):
            step(s, 1, 0)
            if s.r8(0x35E) != lvl:
                break
    elif lvl == 0x11:
        break                         # (Jafar: scenario 1b, from the cobra fight)
    elif lvl == 0x39:
        step(s, 60, 0)
        break
    else:                             # screens between: press on
        step(s, 3, 'B')
        step(s, 20, 0)
print('levels', levels_done, 'final screen %02X' % s.r8(0x35E))

print('1b. Jafar: the cobra fight (tools/states/jafar3.state, made by playing level 18), its last hit point')
s = SNES()
s.module('snes')
s.configure(0, True, 'invincible,inf_apples')
s.ach_reset()
s.load_file(STATES + '/jafar3.state')
for k in range(400):
    for i in range(35):
        b = 0x92B + i * 0x50
        if s.r8(b + 6) == 0x73 and s.r8(b) and 0 < s.r8(b + 0x11) < 0x80:
            s.w8(b + 0x11, 0)
    for b_, n in (('RIGHT', 6), ('A', 3), (0, 3), ('B+A', 8), ('A', 3), (0, 3), ('LEFT', 3), ('A', 3)):
        step(s, n, b_)
    if s.r8(0x1B2B):
        break
step(s, 2400, 0)
print('screen %02X' % s.r8(0x35E))

print('2. pokes')
s = new_game()
step(s, 400, 0)
s.w8(0x365, 9)
step(s, 2, 0)
s.w8(0x365, 10)
step(s, 2, 0)
s.w8(0x364, 9)
step(s, 2, 0)
s.w8(0x364, 10)
step(s, 2, 0)
s.w8(0x36B, 0x98)
step(s, 2, 0)
s.w8(0x36B, 0x01)
step(s, 2, 0)

print('3. the developer code')
s = SNES()
s.module('snes')
s.ach_reset()
s.load_file(STATES + '/title_idle.state')
step(s, 5, 0)
step(s, 3, 'DOWN')
step(s, 20, 0)
step(s, 3, 'DOWN')
step(s, 20, 0)
step(s, 3, 'START')
step(s, 120, 0)
for b in ('L', 'R', 'START', 'SELECT', 'X', 'Y', 'A', 'B'):
    step(s, 3, b, 1)
    step(s, 6, 0, 1)
print('debug flag', s.r8(0x0009))
print('earned:', sorted(set(n for _, n, _ in got)))
