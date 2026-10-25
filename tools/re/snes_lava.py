"""The Lava Cavern's limits in widescreen: python snes_lava.py [widths, e.g. 0,46,106,140] [turbo: 0,4]

The Lava Cavern (level $06) scrolls right by itself (0.625 px a frame). The game holds Aladdin at camera + 240 (16 px
inside the screen's right edge), kills him 48 px past either edge of the screen (left behind) and its objects delete
themselves 32 px past either edge. In widescreen these follow the picture's edges instead (docs/re_snes_wide.md,
section 4). Per width and Remove Slowdown setting (invincible, infinite lives; Aladdin held at a height clear of the
cave, x free):
  wall   : RIGHT held for 700 frames from the start: Aladdin's furthest x from the camera, against the picture's right
           edge (camera + 256 + its right extension) - 16
  behind : then nothing held: where he is left behind (the instant death, $83:8052), against the picture's left edge
           - 48; the level then starts again (camera 0, the edge rule back)
  end    : carried along at the screen's middle to the level's last camera x, then RIGHT held at the exit's height:
           the furthest x (the exit's 4592 at every width: the view stops at the level's end) and the frame the level
           ends (the next level's index or a screen)
  gone   : objects that deleted themselves past the game's screen ($83:9738 / $83:9749) while inside the picture
           (the whole run)
Writes tools/qa/snes/lava_<ext>[_t<turbo>].png (the wall, the moment before he is left behind, the end)."""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa

widths = [int(a) for a in sys.argv[1].split(',')] if len(sys.argv) > 1 else [0, 46, 106, 140]
turbos = [int(a) for a in sys.argv[2].split(',')] if len(sys.argv) > 2 else [0, 4]
LEVEL, EXIT_X, EXIT_Y = 6, 4592, 104
DEATH = 0x838052
GONE_L, GONE_L_KEEP, GONE_R_DEL = 0x839738, 0x839746, 0x839758


def view(s, ext):
    """The picture's world range for the logic's camera now: [camera - left extension, + 256 + 2 * ext)."""
    el = s.view_left()
    el = ext if el is None else el
    cam = s.s16(0x1773)
    return cam - el, cam + 256 + 2 * ext - el


def objects(s):
    return {0x8DB + k * 0x50: s.s16(0x8DB + k * 0x50 + 0xC) for k in range(36) if s.r8(0x8DB + k * 0x50)}


class Run:
    def __init__(self, ext, turbo):
        self.ext = ext
        self.s = s = SNES()
        s.module('snes')
        s.configure(ext, True, 'invincible,inf_lives')
        if turbo:
            s.turbo(turbo)
        s.start_level(LEVEL)
        for pc in (DEATH, GONE_L, GONE_L_KEEP, GONE_R_DEL):
            s.bp(pc)
        self.gone_in, self.gone = 0, 0
        self.deaths = []
        self.shots, self.labels = [], []

    def frame(self, pad, y=None, x=None):
        s = self.s
        if y is not None:
            s.w16(0x8EA, s.r16(0x1777) + y)
        if x is not None:
            s.w16(0x8E7, s.r16(0x1773) + x)
        before = objects(s)
        s.hold(pad)
        s.run(1)
        lo, hi = view(s, self.ext)
        hits = s.bp_hits()
        for i, h in enumerate(hits):
            dp = None
            if h['pc'] == GONE_L:
                nxt = hits[i + 1] if i + 1 < len(hits) else None
                if not (nxt and nxt['pc'] == GONE_L_KEEP and nxt['dp'] == h['dp']):
                    dp = h['dp']
            elif h['pc'] == GONE_R_DEL:
                dp = h['dp']
            elif h['pc'] == DEATH:
                self.deaths.append((s.s16(0x8E7), lo, hi))
            if dp in before:
                self.gone += 1
                self.gone_in += lo - 16 <= before[dp] < hi + 16
        return lo, hi

    def shot(self, what):
        s = self.s
        lo, hi = view(s, self.ext)
        self.shots.append(img(s))
        self.labels.append('%s: x %d, picture %d..%d, camera %d' % (what, s.s16(0x8E7), lo, hi, s.s16(0x1773)))


def check(ext, turbo):
    r = Run(ext, turbo)
    s = r.s
    # the wall: RIGHT held, Aladdin held 60 px below the screen's top (clear of the cave there)
    wall, edge, best = -999, 0, None
    for f in range(700):
        lo, hi = r.frame('RIGHT', y=60)
        d = s.s16(0x8E7) - (hi - 16)
        if f >= 300 and d > wall:
            wall, edge, best = d, hi - 16, f
        if f == 420:
            r.shot('wall')
    # left behind: nothing held
    behind = None
    n0 = len(r.deaths)
    for f in range(2000):
        lo, hi = r.frame(0, y=60)
        if f % 8 == 0 and len(r.deaths) == n0 and s.s16(0x8E7) - lo < -20 and len(r.shots) < 2:
            r.shot('about to be left behind')
        if len(r.deaths) > n0:
            x, lo, hi = r.deaths[n0]
            behind = x - (lo - 48)
            break
    restarted = False
    for f in range(600):
        r.frame(0)
        if s.r16(0x1773) < 16 and s.r8(0x145B) == 3 and s.r8(0x35E) == LEVEL:
            restarted = True
            break
    # the end: carried at the screen's middle to the level's last camera x, then RIGHT at the exit's height
    last = s.r16(0x1A04)
    f = 0
    while s.s16(0x1773) < last and f < 9000:
        r.frame(0, y=60, x=128)
        f += 1
    far, ended = -1, None
    for g in range(1500):
        if s.r8(0x35E) != LEVEL or not s.info()['gameplay']:
            ended = g
            break
        r.frame('RIGHT', y=EXIT_Y)
        far = max(far, s.s16(0x8E7))
        if g == 60:
            r.shot('end')
    out = '%3d %s | wall %+d (x %d, picture edge - 16 %d) | left behind %s | restart %s | end: x %d, %s | ' \
          'objects gone in the picture %d of %d' % (
              ext, 'turbo %d' % turbo if turbo else 'turbo off', wall, edge + wall, edge,
              '%+d from the picture edge - 48' % behind if behind is not None else 'never',
              'yes' if restarted else 'NO', far,
              'level over %d frames after the camera stopped (now %02X)' % (ended + 1, s.r8(0x35E))
              if ended is not None else 'NOT OVER', r.gone_in, r.gone)
    sheet(r.shots, QA + '/lava_%d%s.png' % (ext, '_t%d' % turbo if turbo else ''), cols=1, labels=r.labels)
    ok = wall == 0 and behind is not None and -2 <= behind <= 0 and restarted and ended is not None and \
        far <= EXIT_X and r.gone_in == 0
    return ok, out


bad = 0
for t in turbos:
    for e in widths:
        ok, line = check(e, t)
        bad += not ok
        print(('ok  ' if ok else 'BAD ') + line, flush=True)
print('%d bad' % bad)
