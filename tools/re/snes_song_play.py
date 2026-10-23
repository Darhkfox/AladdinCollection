"""Song uploads ($81:FA23) heard while the bot plays every level (invincible), with the level and camera when each
started: python snes_song_play.py [frames]"""
import os
import random
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa
from snes_wide_play import bot

frames = int(sys.argv[1]) if len(sys.argv) > 1 else 4000
for lv in range(19):
    s = SNES()
    s.module('snes')
    s.configure(0, True, 'invincible,inf_lives,inf_apples')
    s.start_level(lv)
    s.bp(0x81FA23)
    got = []
    done = 0
    while done < frames:
        bot(s, 300, 100000, random.Random(lv * 100 + done), [], [], '', 'invincible,inf_lives,inf_apples')
        done += 300
        for h in s.bp_hits():
            if h['pc'] == 0x81FA23:
                got.append('%02X@f%d lvl%02X cam%d' % (h['a'] & 0xFF, h['frame'], s.r8(0x35E), s.r16(0x1773)))
        s.bp_clear()
        s.bp(0x81FA23)
    print(lv, got)
