"""Which code writes the OAM buffer ($7E:FA80, the low table's x / y bytes) during play in every level: the widescreen
records each sprite's true x at the OAM builder's piece stores ($81:938B, $81:93F9); any other writer of x would need
its own hook. python snes_oam_writers.py [frames]"""
import sys
import os
import random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa
from snes_wide_play import bot

frames = int(sys.argv[1]) if len(sys.argv) > 1 else 1200
total = {}
for lv in range(19):
    s = SNES()
    s.module('snes')
    s.configure(140, True, 'invincible,inf_lives')
    s.start_level(lv)
    s.watch(0x7EFA80, 0x7EFC7F)
    bot(s, frames, 100000, random.Random(lv), [], [], '')
    for h in s.watch_hits(400000):
        if (h['addr'] - 0x7EFA80) % 4 == 0:          # x bytes
            total.setdefault(h['pc'], set()).add(lv)
for pc, lvs in sorted(total.items()):
    print('%06X' % pc, sorted(lvs))
