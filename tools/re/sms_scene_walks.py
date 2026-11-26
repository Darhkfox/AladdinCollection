"""Story scenes: the characters' walks in and out at the sides (docs/re_sms_wide.md section 5a, "Walks").

    python sms_scene_walks.py            # print the module's kSceneWalks table (C++) from the scenes as the game plays them
    python sms_scene_walks.py --show 0 1 40 120     # scene 0, character 1, ticks 40..120: pose, X, Y per tick

Each scene is entered from the title (scene number set, mode $13, as sms_rewind.py --scenes does) and played with no
input; per tick (the scene clock, -($CF79), counted by $6100 while no text is written) every character's pose is taken
from its writer's call: Aladdin's ($2379, pieces at (C40B) in bank (C435), 17 tiles streamed from ROM to patterns
1-17), the second figure's ($3C13, bank $0F, (C690), tiles to 18-34) and the scene's list ($5099, bank $1D, a frame of
the list's own tiles in VRAM; the list entry k by ($CF33) at $50CB). The character's X is the writer's 16-bit X (bc
at $23C2 / $3C63 / $50D4), its Y the byte the writer adds ($C400, $C687, ($CF35)).

A walk in (IN) starts at tick t0, the first tick the game draws the character on its way in (or moves it from where
it waited); the steps from there (pose, X and Y step per tick) repeat with a period the script finds (the walk cycle),
and the module draws the character before t0 by playing that cycle backwards from the character at t0: the game's own
walk, frames and speed, ending exactly where and when the game takes over. A walk out (OUT) goes on from the tick the
game removes a character that walked off (t0), with the cycle of the ticks before. SHOWN: a character standing in
reach of a side (the game keeps it there for its cue) is drawn there from t0 to t1."""
import argparse
import os
import sys

import sms_re as R

# (scene, character, kind, t0, w, note) - checked by eye at 21:9 CRT and square pixels (section 5a). IN: the game's
# walk in is steady from t0 to w (the cycle is taken there); OUT: from w to t0; SHOWN: drawn from t0 to w.
# The throne room ($10 $18 $1A) is drawn at the wide picture's right (scene_at_right): its door is the view's right
# edge, so those who come and go through it need no walk; those from the left walk the longer hall.
# A 7th value ts: the walk in is drawn from that tick only (the clock stands at 17 in $18 while Jasmine speaks: a
# walk begun before would stand mid-hall through her text) and, where the game's speed can't bring the character
# from beyond the view's edge by t0, is played faster (its cycle and its steps alike) from there.
# Rajah in $11 is drawn E px further right (kSceneOuts: cut by the wide edge as by the 4:3 one): no walks.
SPECS = [
    (0x00, 0, 'IN', 58, 138, 'the market: Aladdin walks in to the bread stall'),
    (0x00, 1, 'IN', 494, 640, 'the market: the guard runs in after him'),
    (0x01, 0, 'IN', 365, 429, 'the market: Aladdin comes back for Jasmine'),
    (0x01, 3, 'OUT', 348, 300, 'the market: Abu runs off'),
    (0x06, 1, 'IN', 66, 176, 'the palace: Jasmine walks in'),
    (0x0B, 2, 'IN', 1, 9, 'the escape: the carpet swoops in (from the fade-in on)'),
    (0x0C, 0, 'IN', 49, 59, 'the fall: Aladdin flies in on the carpet'),
    (0x0C, 6, 'IN', 49, 59, 'the fall: the carpet flies in under him'),
    (0x10, 0, 'IN', 18, 120, 'the throne room: Aladdin walks in down the hall (waits at -16 walking on the spot)'),
    (0x11, 1, 'IN', 808, 886, 'night: Jasmine walks in on the balcony'),
    (0x14, 1, 'IN', 298, 380, 'the garden: a guard runs in from the right (put at 240)'),
    (0x18, 0, 'IN', 84, 128, 'the throne room: Aladdin runs in down the hall (waits at -16; from tick 18)', 18),
    (0x20, 1, 'IN', 114, (0x20, 1, 402, 494), "Jafar's room: Abu runs in (his run off later)"),
    (0x23, 4, 'SHOWN', 0, 850, 'the balcony: Iago on the tower'),
]
FAR = 102 + 48   # a ghost is drawn until it is this far beyond the 4:3 picture (the widest side and a character)


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def record(sc, max_frames=12000):
    """{tick: {char: (x, y, bank, ptr, n, tile_base)}} for scene sc, from the title"""
    s = R.boot()
    s.module('sms')
    s.configure(0, True, '')
    s.set_ext(0)
    for f in range(400):
        s.mframes(1, 0)
    s.set8(0xC11F, sc)
    s.goto_mode(0x13)
    for a in (0x610B, 0x2379, 0x3C13, 0x5099, 0x50CB, 0x23C2, 0x3C63, 0x50D4):
        s.bp(a)
    ticks, cur, pend, last_k, started = {}, None, None, None, False
    for f in range(max_frames):
        s.mframes(1, 0)
        for fr, pc, line, banks, r in s.bp_log():
            if pc == 0x610B:
                cur = (0x10000 - r['hl']) & 0xFFFF
                ticks.setdefault(cur, {})
                started = True
                continue
            if cur is None:
                continue
            d = ticks[cur]
            if pc == 0x2379:
                d[0] = [None, s.ram8(0xC400), (banks >> 16) & 0xFF, r['hl'], r['bc'] >> 8, 1]
            elif pc == 0x3C13:
                d[1] = [None, s.ram8(0xC687), 0x0F, r['hl'], r['bc'] >> 8, 18]
            elif pc == 0x5099:
                pend = [None, None, 0x1D, r['hl'], r['bc'] >> 8, 0]
            elif pc == 0x50CB and pend is not None:
                k = (r['hl'] - 0xCF39) // 4
                pend[1] = s.ram8(0xCF37 + 4 * k)
                d[2 + k] = pend
                pend, last_k = None, 2 + k
            elif pc == 0x23C2 and 0 in d and d[0][0] is None:
                d[0][0] = s16(r['bc'])
            elif pc == 0x3C63 and 1 in d and d[1][0] is None:
                d[1][0] = s16(r['bc'])
            elif pc == 0x50D4 and last_k in d and d[last_k][0] is None:
                d[last_k][0] = s16(r['bc'])
        if started and (s.ram8(0xC200) != 0x13 or s.ram8(0xC11F) != sc):
            break
    return ticks


def steps(ticks, c, a, b):
    """[(ptr, dx, dy)] for ticks a..b-1: the pose at t, the move from t to t+1"""
    out = []
    for t in range(a, b):
        e, e1 = ticks[t][c], ticks[t + 1][c]
        out.append((e[3], e1[0] - e[0], e1[1] - e[1]))
    return out


def period(seq, max_len=96):
    """the shortest period the whole sequence keeps (at least 8 ticks of it seen twice)"""
    for L in range(1, min(max_len, len(seq)) + 1):
        if len(seq) - L >= min(8, L) and all(seq[i] == seq[i + L] for i in range(len(seq) - L)):
            return L
    return None


def cycle(ticks, c, t0, w):
    """the steady walk from t0 to w (from a little later if the first ticks are a pose of their own): (steps, s0)"""
    for s0 in range(t0, t0 + 16):
        for cut in range(0, 9):   # (the last ticks may slow to a stop)
            seq = steps(ticks, c, s0, w - cut)
            L = period(seq)
            if L:
                return seq[:L], s0
    raise AssertionError('char %d: no walk cycle from %d to %d' % (c, t0, w))


def entry(get, sc, c, kind, t0, w):
    if kind == 'SHOWN':
        return dict(sc=sc, c=c, kind=kind, t0=t0, t1=w, x=0, y=0, bank=0, n=0, base=0, cyc=[])
    ticks = get(sc)
    e = ticks[t0 if kind == 'IN' else t0 - 1][c]
    x, y, bank, ptr, n, base = e
    if kind == 'IN':
        if isinstance(w, tuple):
            # a walk in too short for a whole cycle: the same character's cycle where it walks longer, from the
            # step that shows the pose at t0 for as many ticks as the game has shown it by then
            bsc, bc, bt0, bw = w
            cyc, s0 = cycle(get(bsc), bc, bt0, bw)
            j = 0
            while ticks[t0 - j - 1][c][3] == ptr:
                j += 1
            L = len(cyc)
            i0 = next(i for i in range(L) if cyc[i][0] == ptr and cyc[i - 1][0] != ptr)
            ph = (i0 + j) % L
        else:
            cyc, s0 = cycle(ticks, c, t0, w)
            L = len(cyc)
            ph = (t0 - s0) % L      # the cycle's step at t0
        cyc = cyc[ph:] + cyc[:ph]
        # until it is well in the 4:3 picture (the module counts it shown on the way)
        t1 = t0
        while t1 + 1 in ticks and c in ticks[t1 + 1] and not (16 <= ticks[t1][c][0] <= 224):
            t1 += 1
    else:
        # the ticks w .. t0 - 1 (it was last drawn at t0 - 1): pose at t and the move into t, latest first
        into = []
        for t in range(t0 - 1, w, -1):
            a, b = ticks[t - 1][c], ticks[t][c]
            into.append((b[3], b[0] - a[0], b[1] - a[1]))
        L = period(into)
        cyc = into[:L][::-1]   # ticks t0 - L .. t0 - 1
        sx = sum(dx for p, dx, dy in cyc)
        t1 = t0 + ((FAR + 256 - x) if sx > 0 else (x + FAR + 32)) * L // max(1, abs(sx)) + L
    return dict(sc=sc, c=c, kind=kind, t0=t0, t1=t1, x=x, y=y, bank=bank, n=n, base=base, cyc=cyc)


def cpp(entries):
    lines = []
    for i, e in enumerate(entries):
        if e['cyc']:
            lines.append('const WalkStep kWalk%d[] = {' % i)
            row = '   '
            for s in e['cyc']:
                item = ' {0x%04X, %d, %d},' % s
                if len(row) + len(item) > 118:
                    lines.append(row)
                    row = '   '
                row += item
            lines.append(row)
            lines.append('};')
    lines.append('const SceneWalk kSceneWalks[] = {')
    for i, e in enumerate(entries):
        cyc = ('kWalk%d' % i) if e['cyc'] else 'nullptr'
        ts = ', %d' % e['ts'] if e.get('ts') is not None else ''
        lines.append('    {0x%02X, %d, WALK_%s, %d, %d, %d, %d, 0x%02X, %d, %d, %d, %s%s},   // %s' % (
            e['sc'], e['c'], e['kind'], e['t0'], e['t1'], e['x'], e['y'], e['bank'], e['n'], e['base'],
            len(e['cyc']), cyc, ts, e['note']))
    lines.append('};')
    return '\n'.join(lines)


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--show', nargs=4, type=int, metavar=('SCENE', 'CHAR', 'T0', 'T1'))
    a = ap.parse_args()
    if a.show:
        sc, c, t0, t1 = a.show
        ticks = record(sc)
        for t in range(t0, t1 + 1):
            e = ticks.get(t, {}).get(c)
            print(t, e if e is None else 'x %s y %s bank %02X ptr %04X n %d base %d' % tuple(e))
        sys.exit()
    cache = {}

    def get(sc):
        if sc not in cache:
            cache[sc] = record(sc)
        return cache[sc]
    out = []
    for sc, c, kind, t0, t1, note, *ts in SPECS:
        e = entry(get, sc, c, kind, t0, t1)
        e['note'] = note
        e['ts'] = ts[0] if ts else None
        out.append(e)
        print('// scene %02X char %d %s t0 %d t1 %d x %d y %d cycle %d' % (sc, c, kind, e['t0'], e['t1'], e['x'], e['y'], len(e['cyc'])), file=sys.stderr)
    print(cpp(out))
