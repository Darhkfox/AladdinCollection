"""A whole stage played through (level select, invincible, sms_wide_shots.Player; the free-scrolling stages driven
by sms_wide_shots' camera patch), a screenshot every N frames, contact sheets of 3 x 6.

    python sms_run_sheet.py <ext> <stage> [--frames 3000] [--every 60] [--start 0]

-> sms_out/run_e<ext>_s<stage>_<n>.png
"""
import argparse
import sms_re as R
from PIL import Image
from sms_wide_shots import drive, FREE, Player


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ext', type=int)
    ap.add_argument('stage', type=int)
    ap.add_argument('--frames', type=int, default=3000)
    ap.add_argument('--every', type=int, default=60)
    ap.add_argument('--start', type=int, default=0)
    a = ap.parse_args()
    s = R.boot()
    s.module('sms')
    s.configure(a.ext, True, 'invincible')
    s.set_ext(a.ext)
    s.start_level(a.stage)
    if a.stage in FREE:
        drive(s, 2)
    p = Player(s, a.stage)
    shots = []
    for f in range(a.frames):
        p.step()
        if f >= a.start and f % a.every == 0:
            px, w, h = s.pixels()
            im = Image.new('RGB', (w, h))
            im.putdata([((p >> 16) & 255, (p >> 8) & 255, p & 255) for p in px])
            shots.append((f, s.module_state(5) & 0xFFFF, im))
        if not (2 <= s.ram8(0xC200) <= 0x0B):
            print('left the stage at', f, hex(s.ram8(0xC200)))
            break
    w = shots[0][2].size[0]
    for n in range(0, len(shots), 18):
        part = shots[n:n + 18]
        sheet = Image.new('RGB', (w * 3, 200 * 6))
        for i, (f, cam, im) in enumerate(part):
            sheet.paste(im, ((i % 3) * w, (i // 3) * 200))
        p = R.OUT + '/run_e%d_s%d_%d.png' % (a.ext, a.stage, n // 18)
        sheet.save(p)
        print(p, [(f, cam) for f, cam, im in part])


if __name__ == '__main__':
    main()
