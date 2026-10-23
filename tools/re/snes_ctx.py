"""Disassembly around addresses: python snes_ctx.py <before-bytes> <after-instr> addr [addr ...] (hex)"""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa

s = SNES()
back = int(sys.argv[1])
n = int(sys.argv[2])
for a in sys.argv[3:]:
    a = int(a, 16)
    print('==== %06X' % a)
    s.dis(a - back, n, m=True, x=True)
