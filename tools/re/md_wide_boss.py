"""Boss fights framed (Aladdin MD, docs/re_md_wide.md section 5, Boss fights). The Cave of Wonders' golden idol: the
camera flies in from the left at the arena's height (map view), Aladdin is dropped in and walks to either end of the
arena and back; per frame the view's left edge in the map (line 0's scroll + the slide) against the camera.

    python tools/re/md_wide_boss.py [ext ...]

Reports, per width: the camera and slide ranges, the view at the arena's ends and in its middle, the camera at which
the fight's framing starts, and the frames on which the view moved more than 3 px further than the camera (a lurch;
the framing holds the view still or eases it in and out at most 3 px a frame). Then the boss object is removed (the fight over)
and the view eases back.
"""
import struct
import sys
from concurrent.futures import ProcessPoolExecutor
from md_util import *
from md_wide_util import view_origin, s16

IN_LEFT = [('UP', lambda x, y: y <= 300), ('DOWN', lambda x, y: y >= 300), ('RIGHT', lambda x, y: x >= 1960)]
WALK = [(120, 'LEFT'), (40, 0), (200, 'RIGHT'), (40, 0), (45, 'LEFT'), (40, 0)]


def cam(md):
    return s16(md.ram16(0xFF7DF6)), s16(md.ram16(0xFF7DF8))


def fight_bit(md):
    return struct.unpack('<8I', md.save()[64:96])[1] >> 2 & 1


def run(E):
    md = ModuleMD(ROM, 'md')
    md.configure(E, True, ('invincible',))
    start_play(md, 5, 1)
    md.mrun(10)
    log = []

    def step(b, n=1):
        md.hold(b)
        for _ in range(n):
            if md.ram8(0xFF7273):
                md.set8(0xFF7273, 0xFF)
            s = md.view()[0] if E else 0
            md.mrun(1)
            mx0 = view_origin(md, 0)[0]
            log.append((mx0 - 16, s, mx0 - E + s, fight_bit(md) if E else 0, md.info & 1))

    md.set8(0xFF7273, 0xFF)
    for b, cond in IN_LEFT:
        for _ in range(3000):
            step(b)
            if cond(*cam(md)):
                break
    step(0, 2)
    md.set8(0xFF7273, 0)
    md.set8(0xFF7E40, 0x83)
    ends = []
    for n, b in WALK:
        step(b, n)
        if b == 0:
            c, s = log[-1][0], log[-1][1]
            ends.append((c, s, c + 16 - E + s, c + 336 + E + s))
    for k in range(32):   # the fight over: the boss object gone
        if md.ram8(0xFF7E40 + k * 0x42 + 0x34) == 0xF5:
            md.set8(0xFF7E40 + k * 0x42, 0)
    step(0, 120)
    start = next((log[i][0] for i in range(1, len(log)) if log[i][3] and not log[i - 1][3]), None)
    lurch = sum(1 for i in range(1, len(log)) if log[i][4] and log[i - 1][4]
                and abs(log[i][2] - log[i - 1][2]) > abs(log[i][0] - log[i - 1][0]) + 3)
    return E, start, ends, lurch, log[-1][1], min(l[1] for l in log), max(l[1] for l in log)


if __name__ == '__main__':
    exts = [int(a) for a in sys.argv[1:]] or [0, 54, 128]
    with ProcessPoolExecutor(8) as ex:
        for E, start, ends, lurch, s_end, s_lo, s_hi in ex.map(run, exts):
            print('E=%3d: framing from camera %s; slide %d..%d; view at the left end, the right end, the middle '
                  '(camera, slide, map x): %s; frames the view moved > 3 px further than the camera: %d; slide %d 120 frames after the '
                  'boss is gone' % (E, start, s_lo, s_hi, ' / '.join('%d %+d %d-%d' % e for e in ends), lurch, s_end),
                  flush=True)
