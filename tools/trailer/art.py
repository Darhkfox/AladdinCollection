"""Our own demo pictures for the "your own covers and background" scene (trailer_work/art/covers): a typographic
portrait cover per console (night sky in the console's colour, crescent moon, palace silhouettes, dunes, an oil
lamp; drawn here with PIL, no game or box art) and a dusk-over-the-desert launcher background (1920x1080).
  python art.py
"""
import os, math, random
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont
from tb import ART

OUT = os.path.join(ART, 'covers')
FONTS = r'C:\Windows\Fonts'
F_TITLE = os.path.join(FONTS, 'georgiab.ttf')
F_DECO = os.path.join(FONTS, 'PAPYRUS.TTF')
F_SUB = os.path.join(FONTS, 'constanb.ttf')
GOLD = (246, 205, 110)


def vgrad(w, h, stops):
    """vertical gradient: stops [(pos 0..1, (r, g, b))]"""
    y = np.linspace(0, 1, h)[:, None]
    out = np.zeros((h, w, 3), np.float32)
    for c in range(3):
        out[..., c] = np.interp(y, [p for p, _ in stops], [col[c] for _, col in stops])
    return Image.fromarray(out.astype(np.uint8))


def stars(d, w, h, n, seed, top=0.0, bottom=0.7):
    rnd = random.Random(seed)
    for _ in range(n):
        x, y = rnd.uniform(0, w), rnd.uniform(top * h, bottom * h)
        r = rnd.choice([0.8, 1.0, 1.2, 1.6, 2.2])
        a = rnd.randint(120, 255)
        d.ellipse((x - r, y - r, x + r, y + r), fill=(255, 250, 230, a))
    for _ in range(n // 25):   # a few four-pointed glints
        x, y = rnd.uniform(0, w), rnd.uniform(top * h, bottom * h * 0.8)
        s = rnd.uniform(5, 11)
        d.polygon([(x, y - s), (x + s * 0.18, y), (x, y + s), (x - s * 0.18, y)], fill=(255, 245, 210, 230))
        d.polygon([(x - s, y), (x, y + s * 0.18), (x + s, y), (x, y - s * 0.18)], fill=(255, 245, 210, 230))


def crescent(img, cx, cy, r, col=(255, 236, 170), glow=True):
    lay = Image.new('RGBA', img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    if glow:
        g = Image.new('RGBA', img.size, (0, 0, 0, 0))
        ImageDraw.Draw(g).ellipse((cx - r * 1.9, cy - r * 1.9, cx + r * 1.9, cy + r * 1.9), fill=col + (70,))
        img.alpha_composite(g.filter(ImageFilter.GaussianBlur(r * 0.6)))
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=col + (255,))
    d.ellipse((cx - r * 0.55, cy - r * 1.1, cx + r * 1.35, cy + r * 0.8), fill=(0, 0, 0, 0))
    img.alpha_composite(lay)


def dome(d, x, base, w, h, col):
    """an onion dome on a drum, with a finial"""
    d.rectangle((x - w * 0.42, base - h * 0.35, x + w * 0.42, base), fill=col)
    pts = []
    for k in range(41):
        t = k / 40
        a = math.pi * t
        rx = w * 0.5 * math.sin(a) * (1.15 - 0.45 * t) + w * 0.02
        pts.append((x - rx, base - h * 0.35 - h * 0.6 * t))
    pts.append((x, base - h * 1.05))
    pts += [(2 * x - px, py) for px, py in reversed(pts[:-1])]
    d.polygon(pts, fill=col)
    d.line((x, base - h * 1.05, x, base - h * 1.22), fill=col, width=max(2, int(w * 0.04)))
    d.ellipse((x - w * 0.04, base - h * 1.18, x + w * 0.04, base - h * 1.1), fill=col)


def minaret(d, x, base, w, h, col):
    d.rectangle((x - w / 2, base - h, x + w / 2, base), fill=col)
    d.rectangle((x - w * 0.8, base - h * 0.72, x + w * 0.8, base - h * 0.68), fill=col)
    d.polygon([(x - w * 0.6, base - h), (x + w * 0.6, base - h), (x, base - h - w * 2.2)], fill=col)


def palace(d, cx, base, scale, col):
    """a palace silhouette: a big central dome, two smaller ones, two minarets, walls with arches"""
    s = scale
    d.rectangle((cx - 330 * s, base - 90 * s, cx + 330 * s, base + 4), fill=col)
    for k in range(-5, 6):   # crenellations
        d.rectangle((cx + k * 60 * s - 12 * s, base - 108 * s, cx + k * 60 * s + 12 * s, base - 90 * s), fill=col)
    dome(d, cx, base - 90 * s, 220 * s, 260 * s, col)
    dome(d, cx - 200 * s, base - 90 * s, 110 * s, 140 * s, col)
    dome(d, cx + 200 * s, base - 90 * s, 110 * s, 140 * s, col)
    minaret(d, cx - 310 * s, base - 90 * s, 34 * s, 300 * s, col)
    minaret(d, cx + 310 * s, base - 90 * s, 34 * s, 300 * s, col)


def town(d, w, base, seed, col, hmin=40, hmax=120):
    """a row of flat-roofed houses and small domes along the bottom"""
    rnd = random.Random(seed)
    x = -20
    while x < w + 20:
        bw = rnd.uniform(50, 120); bh = rnd.uniform(hmin, hmax)
        d.rectangle((x, base - bh, x + bw, base + 2), fill=col)
        if rnd.random() < 0.35:
            dome(d, x + bw / 2, base - bh, bw * 0.7, bw * 0.75, col)
        x += bw - rnd.uniform(0, 12)


def dunes(img, w, h, y0, cols, seed):
    d = ImageDraw.Draw(img)
    rnd = random.Random(seed)
    for k, col in enumerate(cols):
        base = y0 + k * (h - y0) / len(cols) * 0.8
        ph, amp, per = rnd.uniform(0, 6), rnd.uniform(18, 40), rnd.uniform(0.9, 1.6) * w
        pts = [(x, base + amp * math.sin(ph + x / per * 2 * math.pi) + 12 * math.sin(x / 90.0 + k))
               for x in range(0, w + 8, 8)]
        d.polygon(pts + [(w, h), (0, h)], fill=col)


def lamp(d, cx, cy, s, col):
    """a generic oil lamp in silhouette (body, spout, handle, lid)"""
    d.ellipse((cx - 90 * s, cy - 34 * s, cx + 70 * s, cy + 34 * s), fill=col)
    d.polygon([(cx + 40 * s, cy - 18 * s), (cx + 150 * s, cy - 44 * s), (cx + 160 * s, cy - 36 * s),
               (cx + 60 * s, cy + 12 * s)], fill=col)
    d.arc((cx - 150 * s, cy - 36 * s, cx - 60 * s, cy + 30 * s), 90, 270, fill=col, width=int(12 * s))
    d.ellipse((cx - 36 * s, cy - 58 * s, cx + 16 * s, cy - 26 * s), fill=col)
    d.ellipse((cx - 16 * s, cy - 76 * s, cx - 4 * s, cy - 56 * s), fill=col)
    d.rectangle((cx - 40 * s, cy + 30 * s, cx + 20 * s, cy + 46 * s), fill=col)
    d.ellipse((cx - 64 * s, cy + 40 * s, cx + 44 * s, cy + 58 * s), fill=col)


def text_c(d, w, y, text, f, fill, shadow=(0, 0, 0, 170), sx=4):
    tw = d.textbbox((0, 0), text, font=f)[2]
    d.text(((w - tw) // 2 + sx, y + sx), text, font=f, fill=shadow)
    d.text(((w - tw) // 2, y), text, font=f, fill=fill)


COVERS = {
    'md':   ('MEGA DRIVE', 'GENESIS', 'VIRGIN GAMES  \u2022  1993', [(0, (6, 14, 48)), (0.55, (18, 58, 140)), (1, (60, 120, 200))],
             (8, 16, 40)),
    'snes': ('SUPER NINTENDO', 'SUPER NES', 'CAPCOM  \u2022  1993', [(0, (20, 6, 40)), (0.55, (70, 30, 120)), (1, (170, 80, 160))],
             (24, 8, 36)),
    'sms':  ('MASTER SYSTEM', '8-BIT', 'SIMS  \u2022  1994', [(0, (34, 4, 10)), (0.55, (120, 24, 30)), (1, (220, 110, 60))],
             (36, 6, 10)),
}


def cover(gid):
    W, H = 900, 1200
    band, small, sub, sky, sil = COVERS[gid]
    im = vgrad(W, H, sky).convert('RGBA')
    d = ImageDraw.Draw(im, 'RGBA')
    stars(d, W, H, 260, hash(gid) & 0xFFFF, 0.1, 0.65)
    crescent(im, W * 0.5, H * 0.405, 52)
    d = ImageDraw.Draw(im, 'RGBA')
    # palace and town in silhouette, dunes in front
    palace(d, W * 0.5, H * 0.80, 0.95, sil + (255,))
    town(d, W, H * 0.83, 7 + len(gid), sil + (255,), 50, 110)
    dunes(im, W, H, H * 0.86, [(110, 70, 40), (150, 96, 52), (196, 136, 70)], 3)
    d = ImageDraw.Draw(im, 'RGBA')
    lamp(d, W * 0.5, H * 0.945, 0.9, (250, 210, 120, 255))
    # the console band at the top
    d.rectangle((0, 0, W, 118), fill=(10, 10, 18, 235))
    d.rectangle((0, 118, W, 124), fill=GOLD + (255,))
    text_c(d, W, 22, band, ImageFont.truetype(F_SUB, 62), GOLD + (255,), sx=3)
    # the title
    ft = ImageFont.truetype(F_DECO, 190)
    text_c(d, W, 165, 'Aladdin', ft, (255, 226, 140, 255), shadow=(0, 0, 0, 200), sx=6)
    d.line((W * 0.2, 420, W * 0.8, 420), fill=GOLD + (230,), width=3)
    text_c(d, W, 436, sub, ImageFont.truetype(F_SUB, 40), (255, 245, 225, 255), sx=2)
    text_c(d, W, 492, 'AN ALADDIN COLLECTION COVER  \u2022  ' + small, ImageFont.truetype(F_SUB, 24), (230, 220, 200, 200), sx=2)
    # a thin gold frame
    d.rectangle((14, 136, W - 15, H - 15), outline=GOLD + (200,), width=3)
    return im.convert('RGB')


def background():
    W, H = 1920, 1080
    im = vgrad(W, H, [(0, (12, 10, 40)), (0.35, (54, 26, 86)), (0.62, (186, 76, 92)), (0.78, (250, 160, 80)),
                      (1, (250, 190, 110))]).convert('RGBA')
    d = ImageDraw.Draw(im, 'RGBA')
    stars(d, W, H, 420, 11, 0.0, 0.38)
    # a low sun / big moon over the horizon
    g = Image.new('RGBA', im.size, (0, 0, 0, 0))
    ImageDraw.Draw(g).ellipse((1290, 520, 1650, 880), fill=(255, 220, 150, 120))
    im.alpha_composite(g.filter(ImageFilter.GaussianBlur(60)))
    d = ImageDraw.Draw(im, 'RGBA')
    d.ellipse((1380, 610, 1560, 790), fill=(255, 232, 170, 255))
    # far palace and town (lighter), near town (darker), dunes
    palace(d, 560, 840, 1.15, (96, 40, 84, 255))
    town(d, W, 860, 5, (96, 40, 84, 255), 50, 140)
    palace(d, 1700, 900, 0.7, (60, 22, 58, 255))
    town(d, W, 905, 9, (60, 22, 58, 255), 30, 90)
    dunes(im, W, H, 930, [(150, 82, 60), (190, 112, 66), (226, 150, 84)], 21)
    d = ImageDraw.Draw(im, 'RGBA')
    # a flying carpet in silhouette, crossing the sky
    cx, cy = 300, 330
    pts = [(cx + x, cy + 10 * math.sin(x / 40.0)) for x in range(0, 170, 5)]
    d.polygon(pts + [(cx + x, y + 14) for x, y in [(p[0] - cx, p[1]) for p in reversed(pts)]], fill=(40, 16, 46, 255))
    for k in range(0, 170, 34):
        d.line((cx + k, cy + 14 + 10 * math.sin(k / 40.0), cx + k - 4, cy + 26 + 10 * math.sin(k / 40.0)),
               fill=(40, 16, 46, 255), width=3)
    return im.convert('RGB')


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    for gid in COVERS:
        cover(gid).save(os.path.join(OUT, gid + '.png'))
    background().save(os.path.join(OUT, 'background.png'))
    print('covers in', OUT)
