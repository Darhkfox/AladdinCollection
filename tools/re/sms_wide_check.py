"""Background check of the widescreen extension against the hardware's own picture.

    python sms_wide_check.py [ext] [--stages ...] [--frames N]

Renders the background only (debug layers) while the camera moves (auto-scrolling stages as they are, the
free-scrolling ones driven by the test patch of sms_wide_shots, right then left). Every pixel the hardware region
(screen x 8..255) shows is kept by world position (x, line); every extension pixel (and the old blanked column)
at a world position the hardware region has shown, or shows later, is compared with it. Only lines whose scroll is
the camera's count (parallax bands wrap on purpose), only inside the level bounds (outside is masked).
Animated tiles (water, lava, torches) change over time, so a few percent of differences are expected there:
the per-line and per-column breakdown shows whether differences are scattered (animation) or a whole cell/column
(an error).
"""
import argparse
import collections
import sms_re as R
from sms_wide_shots import drive, FREE


def run(ext, st, frames):
    s = R.boot()
    s.module('sms')
    s.configure(ext, True, 'invincible')
    s.set_ext(ext)
    s.start_level(st)
    if st in FREE:
        drive(s, 2)
    s.layers(1)
    y0 = s.ram8(0xC400)
    world = {}
    pending = []
    for f in range(frames):
        if st in FREE:
            s.set8(0xC400, y0)
            if f == frames // 2:
                drive(s, -2)
        s.mframes(1, 'RIGHT')
        if not (2 <= s.ram8(0xC200) <= 0x0B):
            break
        px, w, h = s.pixels()
        cam_drawn = s.module_state(5) & 0xFFFF   # the camera the frame was drawn with
        hs = s.line_hscroll()
        lo = s.ram16(0xC4B9)
        hi = s.ram16(0xC480)
        if st in (2, 3, 4, 6, 9):
            hi = min(hi, lo + s.ram16(0xC489) * 16 - 256)
        if f < 2:
            continue
        for line in range(16, 176, 2):
            if (hs[line] + cam_drawn) & 0xFF:
                continue   # a parallax band
            row = px[line * w:(line + 1) * w]
            sh = s.view_shift(line)    # the view's slide near level ends: output x shows screen x + sh
            for x in range(-ext, 256 + ext):
                wx = cam_drawn + x + sh
                if wx < lo or wx >= hi + 256:
                    continue
                c = row[x + ext]
                if 8 <= x + sh < 256:
                    world.setdefault((wx, line), c)
                else:
                    pending.append((wx, line, c, x + sh))
    bad = collections.Counter()
    cols = collections.Counter()
    n = 0
    for wx, line, c, x in pending:
        ref = world.get((wx, line))
        if ref is None:
            continue
        n += 1
        if ref != c:
            bad[line] += 1
            cols[x] += 1
    total = sum(bad.values())
    return n, total, bad, cols


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('ext', nargs='?', type=int, default=102)
    ap.add_argument('--stages', default='0,1,2,3,4,5,6,7,8,9')
    ap.add_argument('--frames', type=int, default=900)
    a = ap.parse_args()
    for st in [int(x) for x in a.stages.split(',')]:
        n, total, bad, cols = run(a.ext, st, a.frames)
        print('stage %d E=%d: %d compared, %d differ (%.2f%%); worst lines %s; worst x %s' % (
            st, a.ext, n, total, 100.0 * total / max(1, n), bad.most_common(4), cols.most_common(4)))
