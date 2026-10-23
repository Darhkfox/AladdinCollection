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


def level_bounds(s):
    """The camera's range [lo, hi] as the module's level_bounds has it: $C4B9 / $C480, or less where the map ends
    first (a paged map's page table ends where its first page begins; a flat map is $C489 metatiles wide)."""
    lo, hi, st = s.ram16(0xC4B9), s.ram16(0xC480), s.ram8(0xC105)
    if st > 9:
        return lo, hi
    if st in (0, 1, 5, 7, 8):
        rom, bank, table = R.rom(), R.rom()[0x3306 + st], s.ram16(0xC48F)
        rd = lambda a: rom[bank * 0x4000 + (a & 0x3FFF)] | rom[bank * 0x4000 + ((a + 1) & 0x3FFF)] << 8
        first, n = 0x10000, 0
        while n < 64 and table + 2 * n < first:
            first = min(first, rd(table + 2 * n))
            n += 1
        return lo, min(hi, n * 256 - 256)
    return lo, min(hi, lo + s.ram16(0xC489) * 16 - 256)


class Player:
    """Plays a stage through for the whole-stage checks (invincible): Right held, a jump now and then (stage 1's
    rooftops: 8 frames of every 16); Aladdin's height pinned in the free-scrolling stages (driven by the test patch,
    see drive) and in stage 8 (its pits), taken out of stage 1's gaps, and in stages 1 and 8 moved past a wall the
    camera would push him off the screen against. Should a run still end: with retry, a game over takes the play back
    to a checkpoint (every 120 frames, three back) and the jumps' timing changes."""
    def __init__(self, s, st, retry=True):
        self.s, self.st, self.f = s, st, 0
        self.y0 = s.ram8(0xC400)
        self.retry = retry
        self.cp = []
        self.off = 0
        self.tries = 0

    def pad(self):
        f = self.f + self.off
        if self.st == 1:
            return 'RIGHT+2' if f % 16 < 8 else 'RIGHT'
        return 'RIGHT' if (f // 50) % 4 != 3 else 'RIGHT+2'

    def step(self, pad=None):
        s = self.s
        if self.retry and self.f % 120 == 0 and not any(c[0] == self.f for c in self.cp):
            self.cp = self.cp[-5:] + [(self.f, s.save(), s.save_ext())]
        self.prep()
        info = s.mframes(1, pad or self.pad())
        self.f += 1
        if self.retry and s.ram8(0xC200) == 0x11 and self.cp and self.tries < 40:   # game over: back to a checkpoint
            k = max(0, len(self.cp) - 3)
            f, core, ext = self.cp[k]
            self.cp = self.cp[:k + 1]
            s.load(core)
            s.load_ext(ext)
            self.f = f
            self.tries += 1
            self.off = (self.off + 37) % 200
        return info

    def prep(self):
        """the RAM writes before a frame"""
        s = self.s
        if self.st in FREE or self.st == 8:
            s.set8(0xC400, self.y0)
        elif self.st == 1 and s.ram8(0xC400) > self.y0 + 16:
            s.set8(0xC400, self.y0)   # (out of a gap)
        if self.st in (1, 8):
            x = s.ram16(0xC402)
            if x >= 0x8000 or x < 0x30:
                s.set16(0xC402, 0x70)   # (past a wall the camera would push him against)

    def in_stage(self):
        return 2 <= self.s.ram8(0xC200) <= 0x0B


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
