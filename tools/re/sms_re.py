"""Shared helpers for the Master System Aladdin research scripts (tools/re/sms_*.py).

    import sms_re as R
    s = R.boot()                 # SMS() at power-on
    R.dis(s, 0x1234, 20)         # disassemble 20 instructions at a Z80 address (current banking)
    R.disrom(0x1C000, 0x1C080)   # disassemble a ROM range (slot 2 mapping)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
sys.path.insert(0, TOOLS)
from smst import SMS, ROM  # noqa: E402
import z80dis  # noqa: E402

OUT = os.path.join(HERE, 'sms_out')
STATES = os.path.join(TOOLS, 'states')
os.makedirs(OUT, exist_ok=True)
os.makedirs(STATES, exist_ok=True)

_rom = None


def rom():
    global _rom
    if _rom is None:
        _rom = open(ROM, 'rb').read()
    return _rom


def boot():
    return SMS()


def dis(s, addr, n=20, out=print):
    a = addr
    for _ in range(n):
        t, l = z80dis.disasm(s.peek, a)
        out('%04X  %-12s %s' % (a, ' '.join('%02X' % s.peek(a + i) for i in range(l)), t))
        a += l


def disrom(start, end, org=None, out=print):
    """disassemble ROM offsets [start, end); org = Z80 address of start (default: slot 2 for >= $8000)"""
    data = rom()
    if org is None:
        org = start if start < 0x8000 else 0x8000 | (start & 0x3FFF)
    base = start - org
    read = lambda z: data[(z + base) % len(data)]
    for a, n, t in z80dis.dis_range(read, org, org + (end - start)):
        out('%05X %04X  %-12s %s' % (a + base, a, ' '.join('%02X' % read(a + i) for i in range(n)), t))


def png(s, name, scale=1):
    p = os.path.join(OUT, name)
    s.png(p, scale)
    return p


def state_path(name):
    return os.path.join(STATES, name)
