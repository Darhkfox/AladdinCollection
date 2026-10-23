"""The view's slide near level ends: every stage from its start (and the free-scrolling ones driven to the far end
and back, sms_wide_shots' test patch), at the given extensions.

    python sms_slide_check.py [ext ...] [--stages ...] [--frames N] [--shots K]

Per frame (background only, the camera's band, lines 16-175): the picture must move exactly as the view's left edge
(camera - E + slide) moves - a frame-to-frame comparison of every pixel at its world position, so a jump of the
picture or a cell drawn differently than one frame before shows as differences (animated tiles excepted: water,
lava, stage 0's far building, which the game animates with the camera); while the view holds still the parallax
bands must hold still too; and the view must stay inside the level: left edge >= the camera minimum, right edge <= the camera maximum
+ 256 (unless the level is narrower than the view). Also: sprite-only frames where a sprite appears for the first
time inside the view (pop-in). Writes strips sms_out/slide_e<ext>_s<stage>.png (K frames around the start / end).
"""
import argparse
import sms_re as R
from PIL import Image
from sms_wide_shots import drive, FREE


def frame_img(px, w, h):
    im = Image.new('RGB', (w, h))
    im.putdata([((p >> 16) & 255, (p >> 8) & 255, p & 255) for p in px])
    return im


verbose = False


def run(ext, st, frames, nshots, layers):
    s = R.boot()
    s.module('sms')
    s.configure(ext, True, 'invincible')
    s.set_ext(ext)
    s.start_level(st)
    s.layers(layers)
    free = st in FREE
    if free:
        drive(s, 3)
    y0 = s.ram8(0xC400)
    prev = None
    worst = (0, None)
    band_worst = (0, None)
    outside = 0
    jumps = 0
    shots = []
    for f in range(frames):
        if free:
            s.set8(0xC400, y0)
            if f == frames // 2:
                drive(s, -3)
        s.mframes(1, 'RIGHT')
        if not (2 <= s.ram8(0xC200) <= 0x0B):
            break
        cam = s.module_state(5) & 0xFFFF
        vs = s.view_shift(-1)
        left = cam - ext + vs
        lo, hi = s.ram16(0xC4B9), s.ram16(0xC480)
        if st in (2, 3, 4, 6, 9):
            hi = min(hi, lo + s.ram16(0xC489) * 16 - 256)
        vw = 256 + 2 * ext
        if st != 9 and hi + 256 - lo >= vw and (left < lo or left + vw > hi + 256):
            outside += 1
        px, w, h = s.pixels()
        hs = s.line_hscroll()
        cram = bytes(s.cram)
        if prev is not None and cram != prev_cram:
            prev = None   # a fade: the colours change, not the picture
        prev_cram = cram
        # the camera's band: lines whose scroll and shift put the view's left edge where it should be (the line's
        # camera: the one with its scroll's low byte, next to the frame's)
        def line_cam(ln):
            return cam + ((((-hs[ln] - cam) & 0xFF) + 128) & 0xFF) - 128
        lines = [ln for ln in range(16, 176, 2) if line_cam(ln) + s.view_shift(ln) == cam + vs]
        bands = [ln for ln in range(16, 176, 2) if ln not in lines]
        if prev is not None:
            pleft, ppx = prev
            d = left - pleft
            if abs(d) < vw:
                diff = n = 0
                for ln in lines:
                    for x in range(max(0, d), min(w, w + d)):
                        n += 1
                        if px[ln * w + x - d] != ppx[ln * w + x]:
                            diff += 1
                r = diff / max(1, n)
                if r > worst[0]:
                    worst = (r, f)
                if r > 0.25:
                    jumps += 1
                    if verbose:
                        print('  frame %d: %.0f%% differ, cam %d vs %d left %d (before %d), %d level lines' % (f, 100 * r, cam, vs, left, pleft, len(lines)))
                # while the picture holds still, so do the parallax bands (stage 0's floor, stage 1's sky and city)
                if d == 0 and bands:
                    bd = sum(1 for ln in bands for x in range(w) if px[ln * w + x] != ppx[ln * w + x])
                    rb = bd / (len(bands) * w)
                    if rb > band_worst[0]:
                        band_worst = (rb, f)
                    if verbose and rb > 0.05:
                        print('  frame %d: bands %.0f%% differ while holding (cam %d vs %d)' % (f, 100 * rb, cam, vs))
        prev = (left, px)
        if (f < 3 * nshots and f % 3 == 0 and len(shots) < nshots) or f in (frames // 2 - 1, frames - 1):
            shots.append(frame_img(px, w, h))
    if shots:
        sheet = Image.new('RGB', (shots[0].size[0] * 2, 192 * ((len(shots) + 1) // 2)))
        for i, im in enumerate(shots):
            sheet.paste(im, ((i % 2) * im.size[0], (i // 2) * 192))
        sheet.save(R.OUT + '/slide_e%d_s%d%s.png' % (ext, st, '' if layers == 3 else '_bg'))
    return worst, band_worst, outside, jumps, f


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('ext', nargs='*', type=int, default=[22, 74, 102])
    ap.add_argument('--stages', default='0,1,2,3,4,5,6,7,8,9')
    ap.add_argument('--frames', type=int, default=400)
    ap.add_argument('--shots', type=int, default=4)
    ap.add_argument('--sprites', action='store_true', help='draw sprites too (the jump count then includes them)')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    verbose = a.v
    for e in a.ext:
        for st in [int(x) for x in a.stages.split(',')]:
            (worst, band_worst, outside, jumps, f) = run(e, st, a.frames, a.shots, 3 if a.sprites else 1)
            print('E=%3d stage %d: %d frames; level pixels differing frame to frame at most %.1f%% (frame %s), %d frames '
                  'over 25%% (a jump); parallax bands while the view holds: at most %.1f%%; view outside the level: %d frames'
                  % (e, st, f + 1, 100 * worst[0], worst[1], jumps, 100 * band_worst[0], outside))
