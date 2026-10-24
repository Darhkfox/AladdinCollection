"""The boss tune (BOSS TUNE, sequence 50) mid-level (Aladdin MD). Four object types start it from their spawn handler
($4154 table): $18 -> $1B6C5A, $4D -> $1B6CCE, $28 -> $1B6D1E (only while Aladdin's Y $7E04 is in [$2AD, $2F4)),
$F5 -> $1B6D90 (the Cave of Wonders' golden idol).

    python tools/re/md_boss_tune.py where [entries]     where those blocks sit in each level's map
    python tools/re/md_boss_tune.py cave [ext ...]      Cave of Wonders, map view: where the boss object spawns and
                                                        where the tune is queued (GEMS FIFO: $10 50), on two flights -
                                                        along the top then down right of the arena, and in from the left
                                                        at the arena's height
"""
import sys
from md_util import *

TYPES = {0x18: '$18 (H1 $1B6C5A)', 0x28: '$28 (H3 $1B6D1E, Y window)', 0x4D: '$4D (H2 $1B6CCE)', 0xF5: '$F5 (H4 $1B6D90)'}


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def blocks(md):
    W, H = md.ram16(0xFF7DB6), md.ram16(0xFF7DBA)
    out = []
    for row in range(H):
        rp = md.ram32(0xFF9884 + 64 * row) & 0xFFFFFF
        for col in range(W):
            w = md.ram16(rp + 2 * col)
            t = md.ram8(0xFFAE87 + w // 2) if w else 0
            if t in TYPES:
                out.append((TYPES[t], col, row, w // 2))
    return W, H, out


class Fifo:
    """GEMS commands queued since the last look (Z80 RAM $1B40, write index $0036)"""
    def __init__(self, md):
        self.md, self.i, self.buf = md, md.zram[0x36], []

    def new(self):
        z = self.md.zram
        while self.i != z[0x36]:
            self.buf.append(z[0x1B40 + self.i])
            self.i = (self.i + 1) & 0x3F
        cmds = []
        while self.buf:
            if self.buf[0] != 0xFF:
                self.buf.pop(0)
                continue
            if len(self.buf) < 2:
                break
            n = {0x10: 3, 0x12: 3, 0x16: 2}.get(self.buf[1], 2)
            if len(self.buf) < n:
                break
            cmds.append(tuple(self.buf[1:n]))
            del self.buf[:n]
        return cmds


def fly(E, path, cheats=('invincible',)):
    md = ModuleMD(ROM, 'md')
    md.configure(E, True, cheats)
    start_play(md, 5, 1)
    md.mrun(10)
    md.set8(0xFF7273, 0xFF)
    fifo = Fifo(md)
    fifo.new()
    spawned = tune = None
    for b, cond in path:
        md.hold(b)
        for i in range(2000):
            md.mrun(1)
            cx, cy = s16(md.ram16(0xFF7DF6)), s16(md.ram16(0xFF7DF8))
            if spawned is None and any(md.ram8(0xFF7E40 + k * 0x42) and md.ram8(0xFF7E40 + k * 0x42 + 0x34) == 0xF5
                                       for k in range(32)):
                spawned = (cx, cy)
            if tune is None and (0x10, 50) in fifo.new():
                tune = (cx, cy)
            if cond(cx, cy):
                break
    md.hold(0)
    return spawned, tune


PATHS = {
    'top, then down right of the arena': [('UP', lambda x, y: y <= 0), ('RIGHT', lambda x, y: x >= 2100),
                                          ('DOWN', lambda x, y: y >= 700)],
    'in from the left at its height': [('UP', lambda x, y: y <= 300), ('DOWN', lambda x, y: y >= 300),
                                       ('RIGHT', lambda x, y: x >= 2400)],
}

if __name__ == '__main__':
    if sys.argv[1:2] == ['cave']:
        for name, path in PATHS.items():
            for E in [int(a) for a in sys.argv[2:]] or [0, 54, 128]:
                spawned, tune = fly(E, path)
                print('%-34s E=%3d: idol spawned at camera %s, boss tune queued at camera %s' % (name, E, spawned, tune),
                      flush=True)
    else:
        args = sys.argv[2:] if sys.argv[1:2] == ['where'] else sys.argv[1:]
        for e in [int(a) for a in args] or range(13):
            md = ModuleMD(ROM, 'md')
            md.configure(0)
            start_play(md, e, 1)
            W, H, out = blocks(md)
            print('entry %d level %x (%d x %d blocks):' % (e, md.ram8(0xFF7E26), W, H),
                  ', '.join('%s at column %d row %d (map x %d, y %d)' % (t, c, r, c * 16, r * 16) for t, c, r, b in out) or '-')
