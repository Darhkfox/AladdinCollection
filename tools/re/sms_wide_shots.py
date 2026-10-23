"""Widescreen review shots: every stage (and its states) at the given extensions, with the module's hooks.

    python sms_wide_shots.py [ext ...] [--stages 0,1,2] [--frames N] [--hold RIGHT]

Writes sms_out/wide_e<ext>_s<stage>_<k>.png (k = snapshots along the run) and a contact sheet
sms_out/wide_e<ext>.png. Masks from post_frame are drawn as dark red.
"""
import argparse
import sms_re as R
from PIL import Image


def shot(s, info, path):
    px, w, h = s.pixels()
    im = Image.new('RGB', (w, h))
    im.putdata([((p >> 16) & 255, (p >> 8) & 255, p & 255) for p in px])
    ml, mr = info['mask_left'], info['mask_right']
    if ml or mr:
        d = im.load()
        for y in range(16, 176):
            for x in range(ml):
                r, g, b = d[x, y]
                d[x, y] = (r // 3 + 60, g // 4, b // 4)
            for x in range(w - mr, w):
                r, g, b = d[x, y]
                d[x, y] = (r // 3 + 60, g // 4, b // 4)
    im.save(path)
    return im


def drive(s, speed):
    """test only: the free-scrolling stages' camera moves `speed` px a frame on its own ($2BFF: ld a,($C41F) ->
    ld a,speed), so the whole level passes by whatever Aladdin does"""
    s.patch8(0x2BFF, 0x3E)
    s.patch8(0x2C00, speed & 0xFF)
    s.patch8(0x2C01, 0x00)


FREE = (2, 3, 4, 6, 9)


def run(ext, stages, frames, hold, every, cheats='invincible', speed=0):
    s = R.boot()
    s.module('sms')
    sheets = []
    for st in stages:
        s.configure(ext, True, cheats)
        s.set_ext(ext)
        s.start_level(st)   # (the module's level select, so the stage's objects are loaded for this width)
        if speed and st in FREE:
            drive(s, speed)
        ims = []
        y0 = s.ram8(0xC400)
        for f in range(frames):
            if speed and st in FREE:
                s.set8(0xC400, y0)   # (Aladdin rides along at his start height)
            pat = hold
            if hold == 'auto':
                pat = 'RIGHT' if (f // 60) % 4 != 3 else 'RIGHT+2'
            info = s.mframes(1, pat)
            if f % every == every - 1:
                ims.append(shot(s, info, R.OUT + '/wide_e%d_s%d_%d.png' % (ext, st, f // every)))
        sheets.append(ims)
    w = 256 + 2 * ext
    cols = max(len(r) for r in sheets)
    sheet = Image.new('RGB', (w * cols, 192 * len(sheets)))
    for r, ims in enumerate(sheets):
        for c, im in enumerate(ims):
            sheet.paste(im, (c * w, r * 192))
    sheet.save(R.OUT + '/wide_e%d.png' % ext)


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('ext', nargs='*', type=int, default=[40])
    ap.add_argument('--stages', default='0,1,2,3,4,5,6,7,8,9')
    ap.add_argument('--frames', type=int, default=240)
    ap.add_argument('--every', type=int, default=80)
    ap.add_argument('--hold', default='auto')
    ap.add_argument('--drive', type=int, default=0, help='free-scrolling stages: camera speed (test patch)')
    a = ap.parse_args()
    for e in a.ext:
        run(e, [int(x) for x in a.stages.split(',')], a.frames, a.hold, a.every, speed=a.drive)
