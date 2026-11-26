"""Aladdin Collection - PC Edition trailer: app footage (trailer_work/cap, 1920x1080 lossless) -> 1080p60 mp4.
The look of the Mickey & Donald Collection trailers (blurred backdrop, lower-third captions with an accent bar,
chips, before / after wipes, PLUS cards, montage, end card), with a medley of the three games' music (MUSIC).

Every scene is (frame count, render(i)), so lengths are known up front and any frame can be previewed:
  python trailer.py                          full encode (+ the 720p preview)
  python trailer.py preview [scene ...]      middle still of each scene (trailer_work/preview)
  python trailer.py stills scene n           n evenly spaced stills of a scene
  python trailer.py bed                      only the music bed (trailer_work/music_bed.wav) and its loudness
  python trailer.py sheet                    the contact sheet (one still per scene)
"""
import os, sys, json, subprocess
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from tb import CAP, WORK, ART  # noqa: E402

OUT_MP4 = os.path.join(WORK, 'Aladdin_Collection_PC_trailer.mp4')
OUT_720 = os.path.join(WORK, 'Aladdin_Collection_PC_trailer_720p_preview.mp4')
PREVIEW = os.path.join(WORK, 'preview')
FPS = 60
REC_FPS = 59.9227
VW, VH = 1920, 1080
GOLD = (246, 196, 72)
WHITE = (255, 255, 255)
SUBC = (214, 210, 232)
F_BOLD = r'C:\Windows\Fonts\segoeuib.ttf'
F_REG = r'C:\Windows\Fonts\segoeui.ttf'
F_DECO = r'C:\Windows\Fonts\PAPYRUS.TTF'
S = lambda sec: int(round(sec * FPS))
GAME = {'md': ('MEGA DRIVE / GENESIS', (110, 170, 255)), 'snes': ('SUPER NINTENDO', (196, 150, 255)),
        'sms': ('MASTER SYSTEM', (255, 120, 110))}

_fonts = {}
def font(path, size):
    if (path, size) not in _fonts: _fonts[(path, size)] = ImageFont.truetype(path, size)
    return _fonts[(path, size)]


# ---------------------------------------------------------------- footage
class Clip:
    """frames of a recording, decoded by ffmpeg on demand (sequential reads are cheap, a jump back reopens)"""
    def __init__(self, name):
        self.name = name
        self.path = os.path.join(CAP, name + '.mkv')
        w, h, n, _ = open(os.path.join(CAP, name + '.txt')).read().split()
        self.w, self.h, self.n = int(w), int(h), int(n)
        self.p = None; self.pos = 0; self.last = None
        self._rect = None
    def __len__(self): return self.n
    def _open(self, k):
        if self.p: self.p.kill()
        self.p = subprocess.Popen(['ffmpeg', '-v', 'error', '-ss', '%.4f' % (k / REC_FPS), '-i', self.path,
                                   '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'], stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL)
        self.pos = k
    def __getitem__(self, k):
        k = max(0, min(k, self.n - 1))
        if self.last is not None and k == self.pos - 1: return self.last
        if self.p is None or k < self.pos or k > self.pos + 120: self._open(k)
        size = self.w * self.h * 3
        while self.pos <= k:
            buf = self.p.stdout.read(size)
            if len(buf) < size: break
            self.last = np.frombuffer(buf, np.uint8).reshape(self.h, self.w, 3); self.pos += 1
        return self.last
    def rect(self):
        """the game picture's rectangle in the recording (bars around it are black)"""
        if self._rect is None:
            cache = os.path.join(CAP, self.name + '.rect.json')
            if os.path.exists(cache):
                self._rect = tuple(json.load(open(cache)))
            else:
                acc = np.zeros((self.h, self.w), bool)
                for k in (10, self.n // 3, self.n // 2, 2 * self.n // 3, self.n - 10):
                    acc |= self[k].max(axis=2) > 6
                rows, cols = np.nonzero(acc.any(axis=1))[0], np.nonzero(acc.any(axis=0))[0]
                if len(rows) == 0: self._rect = (0, 0, self.w, self.h)
                else:
                    y0, y1, x0, x1 = rows[0], rows[-1] + 1, cols[0], cols[-1] + 1
                    # (snap to the full frame where the bars are thin: HUD-free black edges of a full picture)
                    if y0 < 12 and self.h - y1 < 12: y0, y1 = 0, self.h
                    if x0 < 12 and self.w - x1 < 12: x0, x1 = 0, self.w
                    self._rect = (int(x0), int(y0), int(x1 - x0), int(y1 - y0))
                json.dump(self._rect, open(cache, 'w'))
            self._rect = tuple(self._rect)
        return self._rect

_clips = {}
def clip(name):
    if name not in _clips: _clips[name] = Clip(name)
    return _clips[name]


# ------------------------------------------------------------ frame ops
def backdrop(rgb, rect=None):
    """blurred, darkened full-frame fill made from the picture"""
    if rect: x, y, w, h = rect; rgb = rgb[y:y + h, x:x + w]
    im = Image.fromarray(np.ascontiguousarray(rgb)).resize((96, 54), Image.BILINEAR).filter(ImageFilter.GaussianBlur(2))
    a = np.asarray(im.resize((VW, VH), Image.BILINEAR)).astype(np.uint16)
    return (a * 45 // 100).astype(np.uint8)


TOP = 16          # pictures less tall than the frame sit near the top: the caption goes in the space below


def canvas(name, k, top=True):
    """a recorded gameplay frame; bars around a narrower / wider picture get the blurred backdrop"""
    c = clip(name); rgb = c[k]
    x, y, w, h = c.rect()
    if (w, h) == (VW, VH): return rgb.copy()
    f = backdrop(rgb, (x, y, w, h))
    ty = TOP if (top and w >= VW - 4 and h < VH - 60) else y
    f[ty:ty + h, x:x + w] = rgb[y:y + h, x:x + w]
    return f


UI_SCALE = 0.86

def ui_canvas(rgb):
    """an app screen, a little smaller over its own blurred backdrop (room for the caption)"""
    w, h = int(VW * UI_SCALE), int(VH * UI_SCALE)
    small = np.asarray(Image.fromarray(rgb).resize((w, h), Image.LANCZOS))
    f = backdrop(rgb)
    x, y = (VW - w) // 2, 18
    f[y:y + h, x:x + w] = small
    return f


def fade(frame, a):
    return (frame.astype(np.uint16) * int(max(0, min(1, a)) * 256) >> 8).astype(np.uint8)


def dim(frame, pct):
    return (frame.astype(np.uint16) * pct // 100).astype(np.uint8)


def ease(t):
    t = max(0.0, min(1.0, t)); return 1 - (1 - t) ** 3


# ---------------------------------------------------------------- text
_layer_cache = {}
def caption_layer(head, sub, kicker=None):
    """lower-third box: optional kicker (console name in its colour), heading, sub-lines"""
    key = (head, sub, kicker)
    if key in _layer_cache: return _layer_cache[key]
    fk, fh, fs = font(F_BOLD, 28), font(F_BOLD, 54), font(F_REG, 31)
    d0 = ImageDraw.Draw(Image.new('RGBA', (1, 1)))
    subs = sub.split('\n') if sub else []
    hw = d0.textbbox((0, 0), head, font=fh)[2]
    sw = max([d0.textbbox((0, 0), t, font=fs)[2] for t in subs] or [0])
    kw = d0.textbbox((0, 0), kicker[0], font=fk)[2] if kicker else 0
    top = 40 if kicker else 0
    w = max(hw, sw, kw) + 90; h = 98 + 41 * len(subs) + top
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, w - 1, h - 1), 14, fill=(14, 10, 34, 212))
    d.rectangle((0, 14, 9, h - 15), fill=GOLD + (255,))
    if kicker: d.text((42, 14), kicker[0], font=fk, fill=kicker[1] + (255,))
    d.text((40, 12 + top), head, font=fh, fill=WHITE + (255,))
    for k, t in enumerate(subs): d.text((42, 84 + top + 41 * k), t, font=fs, fill=SUBC + (255,))
    _layer_cache[key] = im
    return im


def overlay(frame, lay, pos, alpha=1.0):
    base = Image.fromarray(frame).convert('RGBA')
    if alpha < 1:
        al = np.asarray(lay).copy(); al[..., 3] = (al[..., 3] * max(0.0, alpha)).astype(np.uint8)
        lay = Image.fromarray(al)
    base.alpha_composite(lay, (int(pos[0]), int(pos[1])))
    return np.asarray(base.convert('RGB')).copy()


def put_caption(frame, head, sub, t_in, t_out, kicker=None, bottom=1048):
    a = ease(t_in / 14) * ease(t_out / 14)
    if a <= 0: return frame
    layer = caption_layer(head, sub, kicker)
    dx = int((1 - ease(t_in / 14)) * -40)
    return overlay(frame, layer, (110 + dx, bottom - layer.size[1]), a)


def caption(f, head, sub, i, n, game=None, bottom=1048):
    if not head: return f
    return put_caption(f, head, sub, i, n - 1 - i, GAME[game] if game else None, bottom)


def centered_text(frame, lines, alpha=1.0):
    """lines: [(text, font_path, size, colour, y)]"""
    lay = Image.new('RGBA', (VW, VH), (0, 0, 0, 0))
    d = ImageDraw.Draw(lay)
    for text, fp, size, col, y in lines:
        f = font(fp, size)
        w = d.textbbox((0, 0), text, font=f)[2]
        d.text(((VW - w) // 2 + 3, y + 4), text, font=f, fill=(0, 0, 0, 170))
        d.text(((VW - w) // 2, y), text, font=f, fill=col + (255,))
    return overlay(frame, lay, (0, 0), alpha)


_chip_cache = {}
def chip_layer(text, size):
    key = (text, size)
    if key not in _chip_cache:
        fnt = font(F_BOLD, size)
        w = ImageDraw.Draw(Image.new('RGBA', (1, 1))).textbbox((0, 0), text, font=fnt)[2]
        im = Image.new('RGBA', (w + 36, size + 26), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        d.rounded_rectangle((0, 0, w + 35, size + 25), 10, fill=(14, 10, 34, 220))
        d.text((18, 6), text, font=fnt, fill=GOLD + (255,))
        _chip_cache[key] = im
    return _chip_cache[key]


def chip(f, text, x=None, y=60, size=40, anchor='left', alpha=1.0):
    """gold chip; x=None: top-right corner; anchor: which edge x refers to"""
    if alpha <= 0 or not text: return f
    lay = chip_layer(text, size)
    w = lay.size[0]
    if x is None: x = VW - 60 - w
    elif anchor == 'center': x -= w // 2
    elif anchor == 'right': x -= w
    return overlay(f, lay, (x, y), alpha)


# ---------------------------------------------------------------- scenes
def segs_index(segs, i):
    """segs [(first frame, frames)]: the segment and frame for output frame i"""
    j = i
    for k, (a, m) in enumerate(segs):
        if j < m: return k, a + j, j, m
        j -= m
    a, m = segs[-1]
    return len(segs) - 1, a + m - 1, m - 1, m


def total(segs): return sum(m for _, m in segs)


def plain(name, segs, head=None, sub=None, game=None, chips=None, kicker=True):
    """gameplay; segs [(first frame, frames)] shown one after another; chips: one per segment (or a single one)"""
    if isinstance(segs, int): segs = [(segs, None)]
    n = total(segs)
    def render(i):
        k, fr, j, m = segs_index(segs, i)
        f = canvas(name, fr)
        c = chips[k] if isinstance(chips, list) else chips
        if c: f = chip(f, c, alpha=ease(j / 8) * ease((m - 1 - j) / 8) if isinstance(chips, list) else 1.0)
        return caption(f, head, sub, i, n, game if kicker else None)
    return n, render


def seq(parts, head, sub, game=None):
    """parts: [(recording, first frame, frames, chip)] played one after another under one caption"""
    n = sum(p[2] for p in parts)
    def render(i):
        j = i
        for name, a, m, c in parts:
            if j < m: break
            j -= m
        f = canvas(name, a + j)
        if c: f = chip(f, c, alpha=ease(j / 8) * ease((m - 1 - j) / 8))
        return caption(f, head, sub, i, n, game)
    return n, render


def ui(parts, head, sub, game=None, mask=None, pointer=None, fill=None):
    """app screens: parts [(recording, first frame, frames)]; mask: (recording, x, y, w, h) blurred (in the
    recording's pixels); pointer: recording whose clicks get a drawn mouse pointer"""
    n = sum(p[2] for p in parts)
    clicks = {}
    if pointer:
        p = os.path.join(CAP, pointer + '.clicks.json')
        clicks = json.load(open(p)) if os.path.exists(p) else []
    def render(i):
        j = i
        for name, a, m in parts:
            if j < m: break
            j -= m
        rgb = clip(name)[a + j]
        if fill and fill[0] == name:   # (a region painted black: e.g. a stray notice in a letterbox bar)
            _, x, y, w, h = fill
            rgb = rgb.copy(); rgb[y:y + h, x:x + w] = 0
        if mask and mask[0] == name:
            _, x, y, w, h = mask
            rgb = rgb.copy()
            rgb[y:y + h, x:x + w] = np.asarray(Image.fromarray(rgb[y:y + h, x:x + w]).filter(ImageFilter.GaussianBlur(9)))
        f = ui_canvas(rgb)
        if pointer == name and clicks:
            f = draw_pointer(f, clicks, a + j)
        return caption(f, head, sub, i, n, game)
    return n, render


_arrow = None
def draw_pointer(f, clicks, k):
    """a mouse pointer gliding to each click (recording frames), pressed for a moment as it clicks"""
    global _arrow
    if _arrow is None:
        im = Image.new('RGBA', (40, 56), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        pts = [(2, 2), (2, 44), (12, 34), (20, 52), (27, 49), (19, 31), (33, 31)]
        d.polygon(pts, fill=(255, 255, 255, 255), outline=(0, 0, 0, 255))
        d.line(pts + [pts[0]], fill=(0, 0, 0, 255), width=2)
        _arrow = im
    pos = None
    prev = (0.62, 0.78)
    for t, x, y in clicks:
        if k < t - 40: break
        if k <= t:
            u = ease((k - (t - 40)) / 34)
            pos = (prev[0] + (x - prev[0]) * u, prev[1] + (y - prev[1]) * u)
            break
        prev = (x, y); pos = prev
    if pos is None: pos = prev
    def sx(v): return (VW - VW * UI_SCALE) / 2 + v * VW * UI_SCALE
    def sy(v): return 18 + v * VH * UI_SCALE
    pressed = any(0 <= k - t < 6 for t, _, _ in clicks)
    lay = _arrow if not pressed else _arrow.resize((34, 48))
    return overlay(f, lay, (sx(pos[0]), sy(pos[1])))


def scene_title(n, name='md_title', a=1100):
    logo = Image.open(os.path.join(ART, 'redtail_logo.png')).convert('RGBA')
    def render(i):
        f = dim(backdrop(canvas(name, a + i)), 80)
        f = fade(f, ease(i / S(1.0)))
        ta = ease((i - S(0.5)) / S(0.8))
        f = centered_text(f, [('THREE CLASSIC VERSIONS  \u2022  ONE COLLECTION', F_BOLD, 44, GOLD, 300),
                              ('Aladdin Collection', F_DECO, 150, WHITE, 352),
                              ('PC EDITION', F_BOLD, 62, WHITE, 560),
                              ('Mega Drive  \u2022  Super Nintendo  \u2022  Master System', F_REG, 40, SUBC, 660)], ta)
        f = overlay(f, logo.resize((200, 198), Image.LANCZOS), ((VW - 200) // 2, 60), ta)
        if i > n - S(0.5): f = fade(f, (n - i) / S(0.5))
        return f
    return n, render


def scene_card(n, lines, name, a):
    """a short text card over dimmed footage"""
    def render(i):
        f = dim(canvas(name, a + i), 38)
        return centered_text(f, lines, ease(i / S(0.4)) * ease((n - 1 - i) / S(0.4)))
    return n, render


def scene_wide3(base, a, n, t1, t2, head, sub, game):
    """the same run at 4:3 (base_w0), 16:9 (base) and 21:9 (base_w4): at t1 the 4:3 picture opens out to 16:9, at t2
    the 21:9 one slides in (its picture shorter and wider)"""
    def render(i):
        k = a + i
        if i < t1 + 24:
            f43 = canvas(base + '_w0', k, top=False)
            if i < t1:
                f = f43
            else:
                f = canvas(base, k)
                x, _, w, _ = clip(base + '_w0').rect()
                m = int((1 - ease((i - t1) / 24)) * x)
                if m > 0:
                    f[:, :m] = f43[:, :m]; f[:, VW - m:] = f43[:, VW - m:]
            lab = '4:3' if i < t1 + 12 else '16:9'
        elif i < t2:
            f = canvas(base, k); lab = '16:9'
        else:
            f = canvas(base + '_w4', k)
            if i < t2 + 20:
                g = canvas(base, k)
                u = ease((i - t2) / 20)
                f = (g.astype(np.float32) * (1 - u) + f.astype(np.float32) * u).astype(np.uint8)
            lab = '21:9'
        f = chip(f, lab, x=VW // 2, y=40, size=46, anchor='center')
        return caption(f, head, sub, i, n, game)
    return n, render


def scene_wipe(a_name, b_name, a, n, head, sub, chip_a, chip_b, game=None, kicker=True):
    """the same moment from two recordings, a moving split: a on the left, b on the right"""
    def render(i):
        fa, fb = canvas(a_name, a + i), canvas(b_name, a + i)
        split = VW // 2 + int(np.sin(i / n * np.pi * 2) * 360)
        f = fa.copy(); f[:, split:] = fb[:, split:]
        f[:, split - 2:split + 2] = 255
        f = chip(f, chip_a, x=split - 30, y=60, anchor='right')
        f = chip(f, chip_b, x=split + 30, y=60)
        return caption(f, head, sub, i, n, game if kicker else None)
    return n, render


def scene_split(base, others, a, per, head, sub, base_chip, game=None):
    """split screen: the left part from `base`, the right from each of `others` [(recording, chip)] in turn"""
    n = per * len(others)
    def render(i):
        other, name_ = others[i // per]
        j = i % per
        f = canvas(base, a + i)
        g = canvas(other, a + i)
        split = VW // 2 + int(np.sin(j / per * np.pi * 2) * 300)
        f[:, split:] = g[:, split:]
        f[:, split - 2:split + 2] = 255
        f = chip(f, base_chip, x=split - 30, y=60, anchor='right')
        f = chip(f, name_, x=split + 30, y=60)
        return caption(f, head, sub, i, n, game)
    return n, render


def scene_filters(names, a, per, region, head, sub):
    """the same run with each filter / shader; a region of the 1080p picture shown at 2x so the filter shows"""
    x0, y0, w, h = region
    n = per * len(names)
    def render(i):
        name, c = names[i // per]
        rgb = clip(name)[a + i]
        crop = rgb[y0:y0 + h, x0:x0 + w]
        img = np.repeat(np.repeat(crop, 2, axis=0), 2, axis=1)
        f = backdrop(rgb)
        ox = (VW - img.shape[1]) // 2
        f[0:img.shape[0], ox:ox + img.shape[1]] = img
        f = chip(f, c)
        return caption(f, head, sub, i, n)
    return n, render


def phone_frame(screen):
    """the phone recording (2400x1080 screenrecord, shown 1520x684) inside a drawn phone: body, rounded screen, camera"""
    sh, sw = screen.shape[:2]
    x0, y0 = (VW - sw) // 2, 110
    f = backdrop(screen)
    im = Image.fromarray(f)
    d = ImageDraw.Draw(im)
    m = 30
    d.rounded_rectangle((x0 - m - 6, y0 - m - 6, x0 + sw + m + 6, y0 + sh + m + 6), 74, fill=(70, 74, 86))
    d.rounded_rectangle((x0 - m, y0 - m, x0 + sw + m, y0 + sh + m), 68, fill=(8, 8, 10))
    a = np.asarray(im).copy()
    mask = Image.new('L', (sw, sh), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, sw - 1, sh - 1), 40, fill=255)
    mk = np.asarray(mask)[..., None] > 0
    a[y0:y0 + sh, x0:x0 + sw] = np.where(mk, screen, a[y0:y0 + sh, x0:x0 + sw])
    im = Image.fromarray(a)
    d = ImageDraw.Draw(im)
    cx, cy = x0 - m // 2 - 2, y0 + sh // 2
    d.ellipse((cx - 7, cy - 7, cx + 7, cy + 7), fill=(28, 30, 38))
    return np.asarray(im).copy()


def scene_phone(n, head, sub):
    def render(i):
        return caption(phone_frame(clip('android_phone_60')[i]), head, sub, i, n)
    return n, render


def scene_features(parts, per, items, heading='PLUS'):
    """parts: [(recording, first frame)] dimmed behind a list that appears line by line"""
    n = per * len(parts)
    def render(i):
        name, a = parts[i // per]
        f = dim(canvas(name, a + i % per), 42)
        lines = [(heading, F_BOLD, 50, GOLD, 230)]
        for k, t in enumerate(items):
            if i >= S(0.4) + k * S(0.7):
                lines.append((t, F_BOLD, 50, WHITE, 320 + k * 88))
        return centered_text(f, lines, ease(i / S(0.4)) * ease((n - 1 - i) / S(0.4)))
    return n, render


def scene_montage(parts, per, head, sub):
    n = per * len(parts)
    def render(i):
        name, a = parts[i // per]
        f = canvas(name, a + i % per)
        return put_caption(f, head, sub, i, n - 1 - i, None)
    return n, render


def scene_end(n, name='md_title', a=1100):
    logo = Image.open(os.path.join(ART, 'redtail_logo.png')).convert('RGBA')
    def render(i):
        f = dim(backdrop(canvas(name, a + (i % 600))), 62)
        f = centered_text(f, [
            ('Aladdin Collection', F_DECO, 120, WHITE, 120),
            ('PC EDITION  \u2022  V1.0  \u2022  WINDOWS & ANDROID  \u2022  FREE', F_BOLD, 48, GOLD, 300),
            ('github.com/Darhkfox/AladdinCollection', F_BOLD, 48, WHITE, 395),
            ('Requires your own ROMs \u2014 not included', F_REG, 38, SUBC, 480),
            ('Aladdin (USA) for Mega Drive / Genesis   \u2022   Aladdin (USA) for SNES   \u2022   '
             'Disney\'s Aladdin (Europe) for Master System', F_REG, 29, SUBC, 535),
            ('Team RedTail  \u2022  patreon.com/TeamRedTail', F_BOLD, 42, WHITE, 640),
            ('x.com/ADH_Gaming   \u2022   youtube.com/adhgaming   \u2022   u/Darhkwing', F_REG, 36, SUBC, 705),
            ('Fan project. Not affiliated with Disney, Sega, Virgin, Capcom or Nintendo.', F_REG, 27, (150, 150, 180), 960),
        ], ease(i / S(0.8)))
        f = overlay(f, logo.resize((180, 178), Image.LANCZOS), ((VW - 180) // 2, 770), ease(i / S(0.8)))
        if i > n - S(1.2): f = fade(f, (n - i) / S(1.2))
        return f
    return n, render


# ---------------------------------------------------------------- timeline
OPT_MASK = ('opt_video', 800, 340, 780, 80)   # the Monitor row's value (the display's name) blurred
LS_SEGS = {'md': [(125, 95), (400, 70)], 'snes': [(125, 95), (470, 70)], 'sms': [(125, 95), (455, 70)]}


def timeline():
    return [
        # ---- opening: the collection, the launcher, the three games
        ('title',      scene_title(S(5.5))),
        ('launcher',   ui([('ui_launcher', 100, 180), ('ui_launcher', 450, 300)], 'ONE LAUNCHER, THREE ALADDINS',
                          'The Mega Drive, Super Nintendo and Master System games, each with a live preview\n'
                          'Keyboard, gamepad or mouse', pointer='ui_launcher')),
        ('style',      ui([('ui_style', 280, 110), ('ui_style', 570, 170)], 'TWO MENU STYLES',
                          'Arabian Nights or Classic, for the launcher and every menu')),
        ('md',         plain('md_market', [(0, S(5))], 'MEGA DRIVE / GENESIS',
                             'Virgin Games  \u2022  1993\nRun on the collection\'s own Mega Drive core', kicker=False)),
        ('snes',       plain('snes_market', [(30, S(5))], 'SUPER NINTENDO',
                             'Capcom  \u2022  1993\nRun on the collection\'s own SNES core', kicker=False)),
        ('sms',        plain('sms_market', [(20, S(5))], 'MASTER SYSTEM',
                             'SIMS  \u2022  1994\nRun on the collection\'s own Master System core', kicker=False)),
        # ---- widescreen
        ('md_wide',    scene_wide3('md_cmp', 0, S(6.5), S(1.8), S(4.2), 'WIDESCREEN UP TO 21:9',
                                   '16:10, 16:9, 2:1 and 21:9 in all three games\n'
                                   'More of each level really drawn at the sides: scenery, enemies and items', 'md')),
        ('md_rug',     plain('md_rug', [(120, S(4.5))], 'THE RUG RIDE, WIDE',
                             'The cave behind the carpet, with the lava wave chasing you', 'md')),
        ('md_title',   ui([('md_title', 250, 90), ('md_title', 480, 120), ('md_title', 845, 60), ('md_title', 1150, 60)],
                          'THE SCREENS AROUND THE LEVELS TOO',
                          'The Genie flies round the logos into the sides; the title, story cards and Level Complete are wide',
                          'md')),
        ('md_jafar',   plain('md_jafar', [(60, S(3.5))], 'BOSS ARENAS FRAMED',
                             'In a boss fight the wide view settles on the arena', 'md')),
        ('snes_wide',  scene_wide3('snes_cmp', 40, S(7.5), S(2.0), S(4.6), 'SUPER NINTENDO, WIDE',
                                   'The sides drawn from the level data the game keeps in RAM\n'
                                   'Every parallax layer and raster band moves as on the console', 'snes')),
        ('snes_lamp',  plain('snes_lamp3', [(80, S(3.5))], 'INSIDE THE LAMP',
                             'The tower\'s gold pillars run on into the sides as a solid gold wall', 'snes')),
        ('sms_wide',   scene_wide3('sms_cmp', 40, S(7), S(2.0), S(4.4), 'MASTER SYSTEM, WIDE',
                                   'Beyond its 256-pixel background, everything is drawn from the level map', 'sms')),
        ('sms_story',  ui([('sms_story', 250, 270), ('sms_intro', 1000, 90)], 'WIDE STORY SCENES',
                          'Each scene\'s picture carried on into the sides; characters walk in from the edge\n'
                          'The Genie\'s intro and the title fill the width too', 'sms')),
        ('square',     scene_wipe('md_sq', 'md_sq_sq', 30, S(3.5), 'CRT OR SQUARE PIXELS',
                                  'Each console\'s picture as on a 4:3 TV, or square pixels', 'CRT', 'SQUARE', 'md')),
        ('hud',        seq([('md_hud_hud0', 30, 105, 'ORIGINAL PLACE'), ('md_hud', 135, 105, 'RE-ANCHORED')],
                           'HUD RE-ANCHORED', 'The status display moves out to the screen\'s edges - or stays put', 'md')),
        # ---- playing
        ('ls',         ui([('ls_md',) + tuple(LS_SEGS['md'][0]), ('ls_md',) + tuple(LS_SEGS['md'][1]),
                           ('ls_snes',) + tuple(LS_SEGS['snes'][0]), ('ls_snes',) + tuple(LS_SEGS['snes'][1]),
                           ('ls_sms',) + tuple(LS_SEGS['sms'][0]), ('ls_sms',) + tuple(LS_SEGS['sms'][1])],
                          'LEVEL SELECT', 'Every level of all three games, straight from the pause menu')),
        ('cheats',     ui([('ch_md', 170, 170), ('ch_md', 650, 140), ('ch_snes', 170, 55), ('ch_sms', 170, 55)],
                          'CHEATS, AND THE DEVELOPERS\' OWN',
                          'The Mega Drive debug level skip, Capcom\'s debug mode and the Master System\'s\n'
                          'AIQY invincibility - plus infinite health, lives and more')),
        ('ach',        ui([('ui_toast', 30, 160), ('ui_ach', 100, 260)], 'ACHIEVEMENTS',
                          '54 to earn across the three games, with pop-ups and an Achievements page\n'
                          'Export to CSV: Playnite\'s columns and a badge for each one',
                          fill=('ui_toast', 0, 942, 1920, 138))),
        ('states',     ui([('st_md', 60, 230)], 'SAVE STATES', 'Five slots per game - F5 saves, F9 loads')),
        ('rewind',     plain('snes_rew', [(240, S(3.8))], 'REWIND', 'Hold R or the left trigger: up to the last minute',
                             'snes', chips=[None], kicker=False)),
        ('ff',         plain('sms_ff', [(20, S(3))], 'FAST FORWARD', 'Hold Tab or the right trigger for 4\u00d7 speed',
                             'sms', chips='FAST FORWARD  4\u00d7', kicker=False)),
        ('slowdown',   ui([('opt_slow', 290, 240)], 'REMOVE SLOWDOWN',
                          'Where the SNES version slowed down, its CPU does the frame\'s work faster:\n'
                          'busy scenes keep a steady 60 frames a second. Off: the original\'s speed', 'snes')),
        # ---- sound
        ('music',      ui([('mp_md', 100, 180), ('mp_sms', 100, 180)], 'MUSIC PLAYER & SOUND TEST',
                          'Every tune and effect, played by each game\'s own sound driver, with channel meters')),
        ('mdplus',     ui([('mp_mdplus', 60, 150), ('mp_mdplus', 450, 130), ('mp_mdplus', 750, 140)],
                          'MD+ CD SOUNDTRACKS',
                          'Bring your own MD+ pack for the Mega Drive game\nTab switches between it and the original',
                          'md')),
        ('msu',        ui([('mp_snes', 60, 130), ('mp_snes', 450, 110), ('snes_msu_play', 140, 210)],
                          'MSU-1 SOUNDTRACKS',
                          'Bring your own MSU-1 pack for the SNES game - here an orchestral one\n'
                          'Switched at once, even in the middle of a level', 'snes')),
        # ---- the picture
        ('filters',    scene_filters([('snes_filt_nearest', 'NEAREST'), ('snes_filt', 'SHARP BILINEAR'),
                                      ('snes_filt_scan', 'SCANLINES'), ('snes_filt_crt', 'CRT'),
                                      ('snes_filt_royale', 'CRT-ROYALE'), ('snes_filt_lottes', 'CRT-LOTTES')], 0, S(0.85),
                                     (480, 200, 960, 540), 'FILTERS AND CRT SHADERS',
                                     'Five built-in filters, or RetroArch slang presets - seven CRT ones included')),
        ('colour',     scene_split('md_col', [('md_col_vivid', 'VIVID'), ('md_col_warm', 'WARM CRT'),
                                              ('md_col_lcd', 'HANDHELD LCD')], 0, S(1.5), 'COLOUR PROFILES',
                                   'Accurate, Linear, Vivid, Warm CRT, Cool, Grayscale, Sepia and Handheld LCD',
                                   'ACCURATE')),
        ('options',    ui([('opt_video', 330, 240)], 'CHANGE IT LIVE',
                          'Widescreen, filters, shaders and colours switch at once from the menu', 'md',
                          mask=OPT_MASK)),
        ('plus',       scene_features([('snes_lamp1', 60), ('md_cave', 60)], S(2.8),
                                      ['High Frame Rate for 100 Hz+ displays (PC)',
                                       'Black frame insertion, V-Sync, integer scaling',
                                       'Xbox, PlayStation and other pads, hot-plugged',
                                       'No sprite flicker  \u2022  screenshots with F12',
                                       'Borderless fullscreen or windowed, any monitor'])),
        # ---- the launcher's extras
        ('covers',     ui([('ui_covers', 40, 170), ('ui_covers', 655, 80), ('ui_covers', 910, 110)],
                          'YOUR OWN COVERS AND BACKGROUND',
                          'PNG or JPEG pictures in the covers folder - none are included (these were made for\n'
                          'this video). Shown whole or filling the frame')),
        ('showgames',  ui([('ui_showgames', 335, 90), ('ui_showgames', 560, 120)], 'SHOW GAMES',
                          'Hide a game you don\'t play - nothing is deleted')),
        ('controls',   ui([('ui_controls', 300, 210)], 'EVERY BUTTON REBINDABLE',
                          'Keyboard and pads, per console - and Xbox or Nintendo menu buttons')),
        ('update',     ui([('ui_update', 40, 110), ('ui_credits', 120, 130)], 'UPDATE NOTIFICATIONS',
                          'A badge on the launcher when a new version is out  \u2022  Credits')),
        ('android',    scene_phone(450, 'ANDROID TOO',
                                   'Phones and tablets: on-screen touch controls or a Bluetooth / USB controller')),
        ('montage',    scene_montage([('md_desert', 60), ('snes_lava', 120), ('sms_escape', 100), ('md_escape', 120),
                                      ('snes_awnw', 100), ('sms_jafar', 100), ('md_dungeon', 150), ('snes_palace', 150)],
                                     S(1.0), 'THREE COMPLETE GAMES', 'From Agrabah to Jafar\'s palace, three times over')),
        ('end',        scene_end(S(8.5))),
    ]




# ---------------------------------------------------------------- sound
# (first scene of the section, recording or 'own' (the scenes' own sound), start offset in seconds)
MUSIC = [('title', 'bed_mdp_title', 0.0), ('md', 'own', 0.0), ('md_wide', 'bed_md_friend', 0.0),
         ('snes_wide', 'bed_snes_agrabah', 0.0), ('square', 'bed_mdp_onejump', 0.0), ('ach', 'bed_msu_agrabah', 0.0),
         ('music', 'own', 0.0), ('filters', 'bed_md_prince', 0.0), ('covers', 'bed_sms_awnw', 0.0),
         ('montage', 'bed_msu_awnw', 0.0)]
XFADE = 1.0
TARGET_RMS = 2600.0
GAIN = {'bed_msu_agrabah': 1.3, 'bed_msu_awnw': 1.5}
SR = 48000


def load_pcm(name, start=0.0, trim=True):
    a = np.fromfile(os.path.join(CAP, name + '.pcm'), np.int16).reshape(-1, 2).astype(np.float32)
    if trim:
        loud = np.nonzero(np.abs(a).max(1) > 300)[0]
        a = a[max(0, (loud[0] if len(loud) else 0) - 200):]
    return a[int(start * SR):]


def own_parts():
    """per scene of an 'own' section: [(recording, first frame, frames)]"""
    return {
        'md': [('md_market', 0, S(5))], 'snes': [('snes_market', 30, S(5))], 'sms': [('sms_market', 20, S(5))],
        'music': [('mp_md', 100, 180), ('mp_sms', 100, 180)],
        'mdplus': [('mp_mdplus', 60, 150), ('mp_mdplus', 450, 130), ('mp_mdplus', 750, 140)],
        'msu': [('mp_snes', 60, 130), ('mp_snes', 450, 110), ('snes_msu_play', 140, 210)],
    }


def make_bed(scenes):
    starts, t, order = {}, 0, []
    for name, (n, _) in scenes:
        starts[name] = t; order.append(name); t += n
    tot = int(t / FPS * SR)
    bed = np.zeros((tot, 2), np.float32)
    xf = int(XFADE * SR)
    sec = lambda f: int(f / FPS * SR)
    parts = own_parts()
    report = []
    for k, (scene, rec, off) in enumerate(MUSIC):
        s0 = sec(starts[scene])
        s1 = sec(starts[MUSIC[k + 1][0]]) if k + 1 < len(MUSIC) else tot
        if rec == 'own':
            seg = np.zeros((s1 - s0 + xf, 2), np.float32)
            for name in order[order.index(scene):]:
                if sec(starts[name]) >= s1: break
                p0 = sec(starts[name]) - s0
                pieces = []
                ramp = int(0.015 * SR)
                for r, a, m in parts.get(name, []):
                    pcm = load_pcm(r, 0, trim=False)
                    o = int(a / REC_FPS * SR)
                    piece = pcm[o:o + sec(m)].copy()
                    if len(piece) < sec(m): piece = np.concatenate([piece, np.zeros((sec(m) - len(piece), 2), np.float32)])
                    piece[:ramp] *= np.linspace(0, 1, ramp)[:, None]
                    piece[-ramp:] *= np.linspace(1, 0, ramp)[:, None]
                    pieces.append(piece)
                if not pieces: continue
                whole = np.concatenate(pieces)
                r = float(np.sqrt((whole ** 2).mean()))
                if r > 1: whole *= min(4.0, max(0.3, TARGET_RMS / r))
                seg[p0:p0 + len(whole)] += whole[:len(seg) - p0]
            a0, a1 = max(0, s0 - xf // 2), min(tot, s1 + xf // 2)
            piece = np.concatenate([np.zeros((s0 - a0, 2), np.float32), seg])[:a1 - a0]
        else:
            seg = load_pcm(rec, off)
            a0, a1 = max(0, s0 - xf // 2), min(tot, s1 + xf // 2)
            piece = seg[:a1 - a0]
            if len(piece) < a1 - a0:
                print('WARNING: %s is %.1f s short' % (rec, (a1 - a0 - len(piece)) / SR))
                piece = np.concatenate([piece, np.zeros((a1 - a0 - len(piece), 2), np.float32)])
            body = piece[s0 - a0:s1 - a0]
            r = float(np.sqrt((body ** 2).mean())) if len(body) else 0.0
            if r > 1: piece = piece * min(4.0, max(0.3, TARGET_RMS / r)) * GAIN.get(rec, 1.0)
        env = np.ones(len(piece), np.float32)
        if k > 0: env[:xf] = np.linspace(0, 1, xf)
        if k + 1 < len(MUSIC): env[-xf:] = np.linspace(1, 0, xf)
        bed[a0:a1] += piece * env[:, None]
        report.append((scene, rec, s0 / SR, s1 / SR))
    fi, fo = int(0.3 * SR), int(3.0 * SR)
    bed[:fi] *= np.linspace(0, 1, fi)[:, None]
    bed[-fo:] *= np.linspace(1, 0, fo)[:, None]
    raw = os.path.join(WORK, 'music_bed_raw.wav')
    write_wav(raw, bed / 32768.0)
    json.dump(report, open(os.path.join(WORK, 'music_sections.json'), 'w'), indent=1)
    return loudnorm(raw, os.path.join(WORK, 'music_bed.wav'))


def write_wav(path, x):
    import wave
    a = np.clip(x, -1, 1)
    with wave.open(path, 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((a * 32767).astype('<i2').tobytes())


def loudnorm(src, dst, I=-14.0, TP=-1.5, LRA=11.0):
    """two-pass EBU R128 normalisation to about -14 LUFS, true peak under -1 dBTP"""
    r = subprocess.run(['ffmpeg', '-hide_banner', '-i', src, '-af', 'loudnorm=I=%g:TP=%g:LRA=%g:print_format=json' % (I, TP, LRA),
                        '-f', 'null', '-'], capture_output=True, text=True).stderr
    j = json.loads(r[r.rindex('{'):r.rindex('}') + 1])
    af = ('loudnorm=I=%g:TP=%g:LRA=%g:measured_I=%s:measured_TP=%s:measured_LRA=%s:measured_thresh=%s:offset=%s:linear=true'
          % (I, TP, LRA, j['input_i'], j['input_tp'], j['input_lra'], j['input_thresh'], j['target_offset']))
    subprocess.run(['ffmpeg', '-y', '-v', 'error', '-i', src, '-af', af + ',aresample=48000', '-ar', '48000', dst], check=True)
    return dst


# ---------------------------------------------------------------- output
def build():
    scenes = timeline()
    tot = sum(n for _, (n, _) in scenes)
    print('total %d frames = %.1f s' % (tot, tot / FPS), flush=True)
    music = make_bed(scenes)
    ff = subprocess.Popen(['ffmpeg', '-y', '-loglevel', 'error',
        '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{VW}x{VH}', '-r', str(FPS), '-i', '-',
        '-i', music, '-map', '0:v', '-map', '1:a', '-c:v', 'libx264', '-preset', 'slow', '-crf', '16',
        '-profile:v', 'high', '-pix_fmt', 'yuv420p', '-c:a', 'aac', '-b:a', '256k', '-ar', '48000', '-ac', '2',
        '-shortest', '-movflags', '+faststart', OUT_MP4], stdin=subprocess.PIPE)
    done = 0
    marks = []
    for k, (name, (n, render)) in enumerate(scenes):
        marks.append((name, done / FPS))
        for i in range(n):
            ff.stdin.write(np.ascontiguousarray(render(i)[:, :, :3]).tobytes())
        done += n
        print(f'scene {k:2d} {name:10s}: {n:5d} frames  (t={done / FPS:6.1f}s)', flush=True)
    ff.stdin.close(); ff.wait()
    json.dump(marks, open(os.path.join(WORK, 'scene_times.json'), 'w'), indent=1)
    subprocess.run(['ffmpeg', '-y', '-v', 'error', '-i', OUT_MP4, '-vf', 'scale=1280:720', '-c:v', 'libx264', '-preset',
                    'medium', '-crf', '22', '-c:a', 'aac', '-b:a', '160k', '-movflags', '+faststart', OUT_720], check=True)
    write_texts()
    contact_sheet()
    print('wrote', OUT_MP4, f'{done / FPS:.1f}s')


def contact_sheet(path=os.path.join(WORK, 'trailer_contact_sheet.png'), cols=6, w=480):
    scenes = timeline()
    h = w * 9 // 16
    rows = (len(scenes) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * w, rows * (h + 26)), (20, 18, 30))
    d = ImageDraw.Draw(sheet)
    t = 0
    for k, (name, (n, render)) in enumerate(scenes):
        im = Image.fromarray(render(n // 2)).resize((w, h), Image.LANCZOS)
        x, y = (k % cols) * w, (k // cols) * (h + 26)
        sheet.paste(im, (x, y + 26))
        d.text((x + 6, y + 4), '%d:%04.1f  %s' % (t // (60 * FPS), (t % (60 * FPS)) / FPS, name), fill=(255, 220, 120),
               font=font(F_BOLD, 16))
        t += n
    sheet.save(path)
    print('wrote', path)


CHAPTERS = [('title', 'Intro & launcher'), ('md', 'The three games'), ('md_wide', 'Widescreen - Mega Drive'),
            ('snes_wide', 'Widescreen - SNES'), ('sms_wide', 'Widescreen - Master System'), ('square', 'Pixels & HUD'),
            ('ls', 'Level select & cheats'), ('ach', 'Achievements'), ('states', 'Save states, rewind & fast forward'),
            ('slowdown', 'Remove Slowdown'), ('music', 'Music player, MD+ & MSU-1 soundtracks'),
            ('filters', 'Filters, shaders & colours'), ('plus', 'More options'),
            ('covers', 'Covers, Show Games & updates'), ('android', 'Android'), ('montage', 'Montage'),
            ('end', 'Download')]


def write_texts():
    """the companion texts (tools/trailer/text) into trailer_work, the description's chapters from the timeline"""
    starts, t = {}, 0
    for name, (n, _) in timeline():
        starts[name] = t; t += n
    ch = '\n'.join('%d:%02d %s' % (starts[s] // FPS // 60, starts[s] // FPS % 60, label) for s, label in CHAPTERS)
    src = os.path.join(HERE, 'text')
    for f in os.listdir(src):
        text = open(os.path.join(src, f), encoding='utf-8').read().replace('{CHAPTERS}', ch)
        open(os.path.join(WORK, f), 'w', encoding='utf-8').write(text)
    print(ch)


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] in ('preview', 'stills'):
        os.makedirs(PREVIEW, exist_ok=True)
        scenes = dict(timeline())
        if sys.argv[1] == 'preview':
            for name in (sys.argv[2:] or list(scenes)):
                n, render = scenes[name]
                Image.fromarray(render(n // 2)).save(os.path.join(PREVIEW, name + '.png')); print(name, n, flush=True)
        else:
            name, cnt = sys.argv[2], int(sys.argv[3])
            n, render = scenes[name]
            for j in range(cnt):
                i = int((j + 0.5) * n / cnt)
                Image.fromarray(render(i)).save(os.path.join(PREVIEW, '%s_%02d.png' % (name, j)))
            print(name, n)
    elif sys.argv[1:] == ['bed']:
        print(make_bed(timeline()))
    elif sys.argv[1:] == ['sheet']:
        contact_sheet()
    elif sys.argv[1:] == ['text']:
        write_texts()
    else:
        build()
