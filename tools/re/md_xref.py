"""Code cross-references for Aladdin (MD): instructions in the code areas that name an absolute address.

    python tools/re/md_xref.py 0xFF7E26 [0xFF7E27 ...]
"""
import struct
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from m68dis import rom, _cs

# code areas of the ROM (coverage over boot, menus and every level)
CODE = [(0x200, 0x4000), (0x1A8000, 0x1B8000), (0x1E5000, 0x1E7000)]


def xr(t, areas=CODE):
    data = rom('md')
    out = []
    pats = [struct.pack('>I', t & 0xFFFFFF)]
    if (t & 0xFFFFFF) >= 0xFF8000:
        pats.append(struct.pack('>H', t & 0xFFFF))
    for lo, hi in areas:
        for p in pats:
            s = lo
            while True:
                i = data.find(p, s, hi)
                if i < 0:
                    break
                s = i + 1
                if i & 1:
                    continue
                for back in range(2, 10, 2):
                    st = i - back
                    ins = list(_cs.disasm(data[st:st + 16], st, 1))
                    if ins and st + ins[0].size > i:
                        txt = ins[0].mnemonic + ' ' + ins[0].op_str
                        if ('%x' % (t & 0xFFFF)) in txt:
                            out.append((st, txt))
                            break
    return sorted(set(out))


if __name__ == '__main__':
    for a in sys.argv[1:]:
        for p, t in xr(int(a, 0)):
            print('%06x  %s' % (p, t))
