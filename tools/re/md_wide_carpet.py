"""The Cave of Wonders' carpet after the golden idol (Aladdin MD, docs/re_md_wide.md section 5, After the fight). The
camera flies in from the left at the arena's height (map view), Aladdin is dropped in, the idol's hit count ($FFF125)
is set to 9 and Aladdin beats it from beside a pedestal: L (the near one, right of it facing left), RL / RR (the far
one, left of it facing right / right of it facing left). Then, from that state, walks of 900 frames: per walk the frame
(and camera X) at which the carpet ($F9, block 144,29) spawns, first shows in the 4:3 part of the view, and picks
Aladdin up ($FFF0D3 = $5E: standing on the carpet).

    python tools/re/md_wide_carpet.py [ext ...]
"""
import sys
from concurrent.futures import ProcessPoolExecutor
from md_util import *
from md_wide_util import s16

IN_LEFT = [('UP', lambda x, y: y <= 300), ('DOWN', lambda x, y: y >= 300), ('RIGHT', lambda x, y: x >= 1960)]
SPOT = {'L': (2080, 'LEFT', 2048), 'RL': (2236, 'RIGHT', 2272), 'RR': (2302, 'LEFT', 2272)}
WALKS = {'still': [(900, 0)], 'right': [(900, 'RIGHT')], 'left': [(900, 'LEFT')],
         'left 90, right': [(90, 'LEFT'), (810, 'RIGHT')]}
IDOL = 0xFF7E40 + 20 * 0x42


def cam(md):
    return s16(md.ram16(0xFF7DF6)), s16(md.ram16(0xFF7DF8))


def carpets(md):
    return [0xFF7E40 + k * 0x42 for k in range(1, 32)
            if md.ram8(0xFF7E40 + k * 0x42) and md.ram8(0xFF7E40 + k * 0x42 + 0x34) == 0xF9]


def in_43_part(md, o):
    """a piece of the object in the sprite table being shown, at screen x 0..319"""
    lo = md.ram32(o + 0x2E)
    if not lo:
        return False
    hi = lo + (md.ram8(o + 0x29) + 1) * 0x80
    v, sat, e = md.vram, (md.regs[5] & 0x7E) << 9, 0
    for _ in range(80):
        a = sat + e * 8
        if lo <= ((v[a + 4] << 8 | v[a + 5]) & 0x7FF) * 32 < hi:
            x = (v[a + 6] << 8 | v[a + 7]) & 0x3FF
            x = (x - 0x400 if x >= 0x300 else x) - 128
            if x + (((v[a + 2] >> 2) & 3) + 1) * 8 > 0 and x < 320:
                return True
        e = v[a + 3] & 0x7F
        if not e:
            break
    return False


def beaten(E, where):
    md = ModuleMD(ROM, 'md')
    md.configure(E, True, ('invincible',))
    start_play(md, 5, 1)
    md.mrun(10)
    md.set8(0xFF7273, 0xFF)
    for b, cond in IN_LEFT:
        md.hold(b)
        for _ in range(3000):
            md.set8(0xFF7273, 0xFF)
            md.mrun(1)
            if cond(*cam(md)):
                break
    md.hold(0)
    md.mrun(2)
    md.set8(0xFF7273, 0)
    md.set8(0xFF7E40, 0x83)
    md.mrun(90)
    md.set8(0xFFF125, 9)   # the next hit is the tenth
    tx, face, ped = SPOT[where]
    faced = False
    for f in range(5000):
        ax = s16(md.ram16(0xFF7E02))
        b = 0
        if abs(ax - tx) > 3:
            b, faced = ('RIGHT' if tx > ax else 'LEFT'), False
        elif not faced:
            b, faced = face, True
        elif md.ram8(IDOL) and s16(md.ram16(IDOL + 2)) == ped and f % 6 < 2:
            b = 'B'
        md.hold(b)
        md.mrun(1)
        if md.ram8(0xFFF112):
            md.hold(0)
            return md
    return None


def run(job):
    E, where = job
    md = beaten(E, where)
    if md is None:
        return E, where, None, {}
    at = cam(md)[0]
    st = md.save()
    out = {}
    for name, walk in WALKS.items():
        md.load(st)
        md.configure(*md.config)
        ev, f = {}, 0
        for n, b in walk:
            md.hold(b)
            for _ in range(n):
                md.mrun(1)
                f += 1
                c = carpets(md)
                if c and 'spawn' not in ev:
                    ev['spawn'] = (f, cam(md)[0])
                if c and 'in the 4:3 part' not in ev and any(in_43_part(md, o) for o in c):
                    ev['in the 4:3 part'] = (f, cam(md)[0])
                if 'picks Aladdin up' not in ev and md.ram8(0xFFF0D3) == 0x5E:
                    ev['picks Aladdin up'] = (f, cam(md)[0])
        out[name] = ev
    return E, where, at, out


if __name__ == '__main__':
    exts = [int(a) for a in sys.argv[1:]] or [0, 54, 128]
    with ProcessPoolExecutor(9) as ex:
        for E, where, at, out in ex.map(run, [(E, w) for w in SPOT for E in exts]):
            print('E=%3d beaten at %-2s (camera %s):' % (E, where, at), flush=True)
            for name, ev in out.items():
                print('    %-15s %s' % (name, ', '.join('%s frame %d camera %d' % (k, v[0], v[1]) for k, v in ev.items())
                                        or 'no carpet'), flush=True)
