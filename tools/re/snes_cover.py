"""Code coverage database and a greppable disassembly listing for SNES Aladdin.

    python tools/re/snes_cover.py listing        # writes tools/re/listing_snes.txt from tools/re/cov_snes.bin

From scripts:
    from snes_cover import Cover
    s.coverage(True); ...run things...; Cover(s).merge()   # ORs this run's flags into tools/re/cov_snes.bin
Flags per ROM byte: bit0 executed with m=1, bit1 m=0, bit2 x=1, bit3 x=0, bit4 emulation mode.
(cov_snes.bin and listing_snes.txt are working files: not committed.)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from snes_common import SNES  # noqa: E402

COV = os.path.join(HERE, 'cov_snes.bin')
LISTING = os.path.join(HERE, 'listing_snes.txt')


class Cover:
    def __init__(self, s):
        self.s = s

    def merge(self):
        new = self.s.covflags_bytes()
        old = open(COV, 'rb').read() if os.path.exists(COV) else bytes(len(new))
        if len(old) != len(new):
            old = bytes(len(new))
        merged = bytes(a | b for a, b in zip(old, new))
        open(COV + '.tmp', 'wb').write(merged)
        os.replace(COV + '.tmp', COV)
        return sum(1 for b in merged if b)


def off_to_pc(off):
    return 0x800000 | ((off >> 15) << 16) | 0x8000 | (off & 0x7fff)


def listing():
    flags = open(COV, 'rb').read()
    s = SNES()
    targets = set()
    lines = []
    for off, f in enumerate(flags):
        if not f:
            continue
        pc = off_to_pc(off)
        m = bool(f & 1) or not (f & 2)
        x = bool(f & 4) or not (f & 8)
        n, t = s.dis1(pc, m, x)
        raw = ' '.join('%02X' % s.r8((pc & 0xff0000) | ((pc + i) & 0xffff)) for i in range(n))
        amb = ''
        if (f & 3) == 3:
            amb += ' ;m?'
        if (f & 12) == 12:
            amb += ' ;x?'
        op = s.r8(pc)
        if op == 0x20:                    # JSR abs
            targets.add((pc & 0xff0000) | s.r16(pc + 1))
        elif op == 0x22:                  # JSL
            targets.add(s.r24(pc + 1))
        lines.append((pc, '%02X:%04X  %-12s %s%s' % (pc >> 16, pc & 0xffff, raw, t, amb)))
    out = []
    norm = lambda a: (a | 0x800000) if (a >> 16) < 0x40 else a  # noqa: E731
    tset = set(norm(t) for t in targets)
    for pc, l in lines:
        if pc in tset:
            out.append('')
            out.append('sub_%06X:' % pc)
        out.append(l)
    open(LISTING, 'w', encoding='utf-8').write('\n'.join(out) + '\n')
    return LISTING, len(lines)


if __name__ == '__main__':
    if len(sys.argv) >= 2 and sys.argv[1] == 'listing':
        print(listing())
