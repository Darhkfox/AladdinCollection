"""Draws the app icon (res/app.ico): a golden magic lamp under a crescent moon on a night-sky disc.
Run once; the .ico is committed."""
import os
import random
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def draw(size):
    S = 512
    im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    # disc: Agrabah's night sky, indigo warming to purple, with a gold rim
    sky = Image.new('RGBA', (S, S))
    sd = ImageDraw.Draw(sky)
    for y in range(S):
        t = y / S
        sd.line([(0, y), (S, y)], fill=(int(10 + 50 * t), int(9 + 18 * t), int(36 + 40 * t), 255))
    mask = Image.new('L', (S, S), 0)
    ImageDraw.Draw(mask).ellipse((8, 8, S - 8, S - 8), fill=255)
    im.paste(sky, (0, 0), mask)
    d = ImageDraw.Draw(im)
    rnd = random.Random(11)
    for _ in range(45):
        x, y = rnd.uniform(70, S - 70), rnd.uniform(60, S * 0.55)
        if (x - S / 2) ** 2 + (y - S / 2) ** 2 > (S / 2 - 60) ** 2:
            continue
        r = rnd.choice([2, 2, 3, 4])
        d.ellipse((x - r, y - r, x + r, y + r), fill=(255, 246, 220, rnd.randint(140, 255)))
    # crescent moon
    mx, my, mr = S * 0.68, S * 0.30, S * 0.11
    d.ellipse((mx - mr, my - mr, mx + mr, my + mr), fill=(246, 214, 122, 255))
    d.ellipse((mx - mr * 0.55, my - mr * 1.1, mx + mr * 1.45, my + mr * 0.9), fill=(20, 15, 48, 255))
    # the lamp: glow, then body, spout, handle, lid
    glow = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse((S * 0.14, S * 0.50, S * 0.86, S * 0.86), fill=(255, 190, 70, 150))
    glow = glow.filter(ImageFilter.GaussianBlur(40))
    im = Image.alpha_composite(im, glow)
    d = ImageDraw.Draw(im)
    gold, dark, light = (236, 180, 60, 255), (150, 96, 24, 255), (255, 232, 150, 255)
    # spout: a tapering curve to the left
    d.polygon([(S * 0.36, S * 0.64), (S * 0.12, S * 0.53), (S * 0.09, S * 0.555), (S * 0.30, S * 0.72)], fill=gold, outline=dark)
    # handle on the right
    d.arc((S * 0.62, S * 0.56, S * 0.86, S * 0.76), 270, 90, fill=gold, width=22)
    # body
    d.ellipse((S * 0.26, S * 0.58, S * 0.72, S * 0.80), fill=gold, outline=dark, width=6)
    d.chord((S * 0.30, S * 0.60, S * 0.66, S * 0.70), 190, 340, fill=light)
    # foot
    d.rounded_rectangle((S * 0.38, S * 0.78, S * 0.60, S * 0.84), radius=10, fill=dark)
    # lid and finial
    d.ellipse((S * 0.39, S * 0.535, S * 0.59, S * 0.615), fill=gold, outline=dark, width=5)
    d.ellipse((S * 0.465, S * 0.49, S * 0.515, S * 0.545), fill=light, outline=dark, width=4)
    # a wisp of smoke from the spout
    wisp = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    wd = ImageDraw.Draw(wisp)
    for i, (x, y, r) in enumerate([(0.11, 0.49, 0.035), (0.14, 0.43, 0.045), (0.12, 0.36, 0.055), (0.17, 0.29, 0.06)]):
        wd.ellipse((S * (x - r), S * (y - r), S * (x + r), S * (y + r)), fill=(120, 190, 255, 150 - i * 25))
    wisp = wisp.filter(ImageFilter.GaussianBlur(6))
    im = Image.alpha_composite(im, wisp)
    d = ImageDraw.Draw(im)
    d.ellipse((10, 10, S - 10, S - 10), outline=(242, 193, 78, 255), width=16)
    out = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    out.paste(im, (0, 0), mask)
    return out.resize((size, size), Image.LANCZOS)


if __name__ == '__main__':
    big = draw(256)
    big.save(os.path.join(ROOT, 'res', 'app.ico'), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    big.save(os.path.join(ROOT, 'res', 'app_icon_preview.png'))
    print('res/app.ico written')
