"""Every place the game asks for a song (LDA #n / JSL $81:FA23) or a sound effect (LDA #n / JSL $81:FA00 or $81:F9D4)
with an immediate id, grouped by id: python snes_sound_calls.py"""
import os
import re
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import ROM  # noqa

rom = open(ROM, 'rb').read()


def pc(o):
    return 0x800000 | ((o >> 15) << 16) | 0x8000 | (o & 0x7FFF)


for name, target in (('song', b'\x22\x23\xFA\x81'), ('sfx', b'\x22\x00\xFA\x81'), ('F9D4', b'\x22\xD4\xF9\x81'),
                     ('obj sfx', b'\x22\xC2\x90\x81')):
    ids = {}
    for m in re.finditer(re.escape(target), rom):
        o = m.start()
        if rom[o - 2] == 0xA9:
            ids.setdefault(rom[o - 1], []).append(pc(o))
    print(name)
    for k in sorted(ids):
        print('  %02X: %s' % (k, ' '.join('%06X' % p for p in ids[k][:8])))
