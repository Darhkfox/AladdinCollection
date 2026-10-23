"""Background check of the widescreen extension against the hardware's own picture.

    python sms_wide_check.py [ext] [--stages ...] [--frames N]

Renders the background only (debug layers) while the camera moves (the stages played through by
sms_wide_shots.Player, the free-scrolling ones driven by its test patch, right then left), twice: every pixel the
hardware region (screen x 8..255) shows is kept by world position (x, line), then every extension pixel (and the
old blanked column) at a world position the hardware region showed at some time is compared with it. Only lines
whose scroll is the camera's count (parallax bands wrap on purpose), only inside the level (sms_slide_check checks
that the view stays there).
Animated tiles (water, lava, torches, stage 1's railings) change over time: an extension pixel counts as wrong
only if the hardware region never showed its colour at that world position (any phase of the animation will do);
the per-line and per-column breakdown shows whether differences are scattered or a whole cell/column (an error).
--bad writes the frames with the most wrong pixels, wrong pixels in magenta (sms_out/wcheck_e<ext>_s<st>_<f>.png).
"""
import argparse
import array
import collections
import sms_re as R
from sms_wide_shots import drive, FREE, Player, level_bounds


def play(ext, st, frames):
    """The stage's frames as drawn (background only): (frame, pixels, width, the camera it was drawn with, the lines'
    scroll and view shift, the view's shift, the level's bounds)"""
    s = R.boot()
    s.module('sms')
    s.configure(ext, True, 'invincible')
    s.set_ext(ext)
    s.start_level(st)
    if st in FREE:
        drive(s, 2)
    s.layers(1)
    p = Player(s, st)
    for f in range(frames):
        if st in FREE and f == frames // 2:
            drive(s, -2)
        p.step()
        if not p.in_stage():
            break
        if f < 2:
            continue
        px, w, h = s.pixels()
        yield (f, px, w, s.module_state(5) & 0xFFFF, s.line_hscroll(), [s.view_shift(ln) for ln in range(192)],
               s.view_shift(-1), level_bounds(s))


def cells(ext, fr):
    """(world x, line, colour, screen x, output x) of a frame's camera band: the lines whose camera (the one with the
    scroll's low byte, next to the frame's) plus their shift put the view where the frame's view is (parallax bands
    wrap on purpose); inside the level only"""
    f, px, w, cam, hs, shl, vs, (lo, hi) = fr
    for line in range(16, 176, 2):
        lc = cam + ((((-hs[line] - cam) & 0xFF) + 128) & 0xFF) - 128
        sh = shl[line]
        if lc + sh != cam + vs:
            continue
        for o in range(w):
            x = o - ext + sh
            wx = lc + x
            if lo <= wx < hi + 256:
                yield wx, line, px[line * w + o], x, o


def run(ext, st, frames, every=1, show=0):
    # pass 1: what the hardware region (screen x 8..255) shows at each world position, all frames
    world = {}
    for fr in play(ext, st, frames):
        for wx, line, c, x, o in cells(ext, fr):
            if 8 <= x < 256:
                world.setdefault((wx, line), set()).add(c)
    # pass 2 (the same run): the sides of every n-th frame against it
    bad = collections.Counter()
    cols = collections.Counter()
    per_frame = collections.defaultdict(list)
    frames_px = {}
    n = 0
    for fr in play(ext, st, frames):
        f = fr[0]
        if f % every:
            continue
        for wx, line, c, x, o in cells(ext, fr):
            if 8 <= x < 256:
                continue
            ref = world.get((wx, line))
            if ref is None:
                continue
            n += 1
            if c not in ref:
                bad[line] += 1
                cols[x] += 1
                per_frame[f].append((o, line))
        if show and per_frame.get(f):
            frames_px[f] = (array.array('I', fr[1]), fr[2])
            if len(frames_px) > 4 * show:   # (keep the worst few)
                keep = sorted(frames_px, key=lambda k: -len(per_frame[k]))[:2 * show]
                frames_px = {k: frames_px[k] for k in keep}
    total = sum(bad.values())
    worst = sorted(per_frame.items(), key=lambda kv: -len(kv[1]))
    if show:
        from PIL import Image
        for f, pts in worst[:show]:
            if f not in frames_px:
                continue
            px, w = frames_px[f]
            h = len(px) // w
            im = Image.new('RGB', (w, h))
            im.putdata([((p >> 16) & 255, (p >> 8) & 255, p & 255) for p in px])
            for o, line in pts:
                im.putpixel((o, line), (255, 0, 255))
            im.resize((w * 2, h * 2), Image.NEAREST).save(R.OUT + '/wcheck_e%d_s%d_%d.png' % (ext, st, f))
    return n, total, bad, cols, [(f, len(p)) for f, p in worst[:6]]


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('ext', nargs='?', type=int, default=102)
    ap.add_argument('--stages', default='0,1,2,3,4,5,6,7,8,9')
    ap.add_argument('--frames', type=int, default=900)
    ap.add_argument('--every', type=int, default=1, help='check the sides of every n-th frame')
    ap.add_argument('--bad', type=int, default=0, help='write the n frames with the most wrong pixels')
    a = ap.parse_args()
    for st in [int(x) for x in a.stages.split(',')]:
        n, total, bad, cols, worst = run(a.ext, st, a.frames, a.every, a.bad)
        print('stage %d E=%d: %d compared, %d differ (%.2f%%); worst lines %s; worst x %s; worst frames %s' % (
            st, a.ext, n, total, 100.0 * total / max(1, n), bad.most_common(4), cols.most_common(4), worst))
