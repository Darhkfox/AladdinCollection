"""Widescreen review strips for Aladdin (MD) through the port's module (build/tools/mdtool.dll).

    python tools/re/md_wide_demo.py [ext ...]         # every level: start, walk right, back left -> md_out/wide_<ext>.png
    python tools/re/md_wide_demo.py caves             # disassemble the patched caves (ext 128)
"""
import sys
from md_util import *

LEVELS = None


def module(ext=0, cheats=()):
    md = ModuleMD(ROM, 'md')
    md.configure(ext, True, cheats)
    return md


def info(md):
    i = md.info
    return 'g%d m%d/%d' % (i & 1, (i >> 8) & 255, (i >> 16) & 255)


def strip(ext, entries=None, frames=((60, ''), (90, 'RIGHT'), (90, 'RIGHT+C'), (60, 'RIGHT'), (90, 'LEFT'))):
    md = module(ext)
    names = md.levels()
    entries = entries if entries is not None else range(len(names))
    ims, lab = [], []
    for e in entries:
        md.configure(ext)
        ok = md.start_level(e, 1)
        for n, (f, b) in enumerate(frames):
            md.hold(b)
            md.mrun(f)
            ims.append(shot(md)); lab.append('%d %s %s %s' % (e, names[e][1][:18], b, info(md)))
        md.hold(0)
    grid(ims, len(frames), lab, OUT + '/wide_%d.png' % ext)


if __name__ == '__main__':
    if sys.argv[1:2] == ['caves']:
        md = module(128)
        rom = bytes(md.rom[0x7000:0x7200])
        from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000
        cs = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
        for base in (0, 0x80, 0x100, 0x180):
            print('--- %x' % (0x7000 + base))
            for i in cs.disasm(rom[base:base + 0x80], 0x7000 + base):
                print('%06x  %-8s %s' % (i.address, i.mnemonic, i.op_str))
                if i.mnemonic == 'rts' and i.address > 0x7000 + base + 8:
                    break
        sys.exit()
    for a in sys.argv[1:] or ['128']:
        strip(int(a))
