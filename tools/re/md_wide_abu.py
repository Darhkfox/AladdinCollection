"""The Sultan's Palace's carpet over the hole (Aladdin MD, docs/re_md_game.md section 1, $FFF11C). Abu hangs in a cage
over the top corridor (map type $57, block 154,8); the floor of the room below the rope has a hole (blocks 182-184, 23):
the shaft down to the room with the level's exit. The carpet in the hole (map type $82, block 183,23: object $5F) is a
lid that spawns only while Abu hasn't been freed ($FFF11C clear). One state in the corridor (made at 4:3, map view),
then the same input at every width:
  skip - walk right under the cage, down the rope, left onto the hole: the carpet is there, Aladdin stands on it
  free - jump into the cage first (Abu freed), then the same: no carpet, Aladdin drops down the shaft (y 1206)
  back - skip, then back up to the corridor (map view), free Abu, the route again: the carpet gone, down the shaft

    python tools/re/md_wide_abu.py [ext ...]
"""
import sys
from md_util import *
from md_wide_util import s16

CARPET, CAGE_X = 0x5F, 2462


def cam(md):
    return s16(md.ram16(0xFF7DF6)), s16(md.ram16(0xFF7DF8))


def carpets(md):
    return [0xFF7E40 + k * 0x42 for k in range(1, 32) if md.ram8(0xFF7E40 + k * 0x42) == CARPET]


def goto(md, tx, ty, settle):
    """map view to camera (tx, ty), then Aladdin back in"""
    md.set8(0xFF7273, 0xFF)
    for _ in range(4000):
        cx, cy = cam(md)
        b = '+'.join(([('RIGHT' if cx < tx else 'LEFT')] if abs(cx - tx) > 4 else []) +
                     ([('DOWN' if cy < ty else 'UP')] if abs(cy - ty) > 4 else []))
        if not b:
            break
        md.hold(b)
        md.set8(0xFF7273, 0xFF)
        md.mrun(1)
    md.hold(0)
    md.mrun(2)
    md.set8(0xFF7273, 0)
    md.set8(0xFF7E40, 0x83)
    md.mrun(settle)


def route(md, free, n=2000):
    ev, phase, last_ay, pf = {}, 'cage' if free else 'right', None, 0
    for f in range(n):
        ax, ay = s16(md.ram16(0xFF7E02)), s16(md.ram16(0xFF7E04))
        if phase == 'cage':
            b = 'RIGHT' if ax < CAGE_X - 3 else ('LEFT' if ax > CAGE_X + 3 else ('C' if f % 40 < 6 else 0))
            if md.ram8(0xFFF11C) and f % 40 > 30:
                phase = 'right'
        if phase == 'right':
            b = 'RIGHT'
            if ax >= 3060:
                phase, pf = 'down', f
        if phase == 'down':
            b = 'DOWN'
            if f - pf > 30 and ay > 500 and ay == last_ay:
                phase = 'left'
        if phase == 'left':
            b = 'LEFT' if ax > 2930 else 0
        last_ay = ay
        md.hold(b)
        md.mrun(1)
        if md.ram8(0xFFF11C) and 'freed' not in ev:
            ev['freed'] = f
        c = carpets(md)
        if c and 'carpet' not in ev:
            ev['carpet'] = (f, s16(md.ram16(c[0] + 2)), s16(md.ram16(c[0] + 4)))
        if md.ram8(0xFFF0D3) == CARPET and 'on it' not in ev:
            ev['on it'] = f
    ev['Aladdin'] = (s16(md.ram16(0xFF7E02)), s16(md.ram16(0xFF7E04)))
    return ev


if __name__ == '__main__':
    exts = [int(a) for a in sys.argv[1:]] or [0, 54, 128]
    md = ModuleMD(ROM, 'md')
    md.configure(0, True, ('invincible',))
    start_play(md, 10, 1)
    md.mrun(10)
    goto(md, 2250, 40, 30)
    base = md.save()
    for E in exts:
        out = {}
        for name in ('skip', 'free', 'back'):
            md = ModuleMD(ROM, 'md')
            md.configure(E, True, ('invincible',))
            md.load(base)
            md.configure(E, True, ('invincible',))
            if name == 'back':
                route(md, False, 700)
                goto(md, 2250, 40, 30)
                out['back: carpets left at the cage'] = len(carpets(md))
            out[name] = route(md, name != 'skip', 700 if name == 'skip' else 2000)
        print('E=%3d' % E, out, flush=True)
