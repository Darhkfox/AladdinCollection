"""Which sound effects play where: every request to the driver ($DE06-$DE08, ids $95-$D0) during play of every stage
(level select, invincible, Right held with jumps / throws), with what happened in the same frame: the stage, Aladdin's
state ($C406), health / stones changes, an object waking or finishing, the type Aladdin touched ($C43B).

    python sms_sfx_context.py [--frames 2500]
"""
import argparse
import collections
import sms_re as R
from sms_wide_shots import drive, FREE


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=2500)
    a = ap.parse_args()
    ctx = collections.defaultdict(collections.Counter)
    for st in range(10):
        for cheats in ('invincible', ''):
            s = R.boot()
            s.module('sms')
            s.configure(0, True, cheats)
            s.start_level(st)
            s.set8(0xC456, 9)
            s.watch(0xDE06, 0xDE08)
            prev = bytes(s.ram)
            for f in range(a.frames):
                b = ['RIGHT', 'RIGHT+2', 'RIGHT', '1', 'LEFT', 'RIGHT+2', 'DOWN', 'UP+2'][(f // 25) % 8]
                s.run(1, b)
                cur = bytes(s.ram)
                ev = []
                if cur[0x116] < prev[0x116]: ev.append('health-')
                if cur[0x116] > prev[0x116]: ev.append('health+')
                if cur[0x456] < prev[0x456]: ev.append('stone-')
                if cur[0x456] > prev[0x456]: ev.append('stone+')
                if cur[0x406] != prev[0x406]: ev.append('state%02X' % cur[0x406])
                for i in range(16):
                    o = 0xA00 + i * 0x40
                    if cur[o + 0x14] and not prev[o + 0x14]: ev.append('obj%02Xdone' % cur[o + 0x15])
                if cur[0x43B] and cur[0x43B] != prev[0x43B]: ev.append('touch%02X' % cur[0x43B])
                if cur[0x200] != prev[0x200]: ev.append('mode%02X' % cur[0x200])
                for fr, pc, ad, line, v in s.watch_log():
                    if 0x95 <= v <= 0xD0:
                        ctx[v]['st%d' % st] += 1
                        ctx[v]['pc%04X' % pc] += 1
                        for e in ev:
                            ctx[v][e] += 1
                prev = cur
                if s.ram8(0xC200) in (0x11, 0x13):
                    break
    for v in sorted(ctx):
        print('%02X' % v, ' '.join('%s:%d' % kv for kv in ctx[v].most_common(9)))


if __name__ == '__main__':
    main()
