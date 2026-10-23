"""Shared helpers for the Mega Drive Aladdin RE scripts (md_*).

    import sys; sys.path.insert(0, r'K:\\projects\\Aladdin\\tools\\re')
    from md_util import *
"""
import os
import sys
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)

from mdt import MD, ModuleMD, ROM, BTN  # noqa: E402
from m68dis import dis, insns, xrefs, calls  # noqa: E402

try:
    from PIL import Image, ImageDraw
except ImportError:  # pragma: no cover
    Image = None

OUT = os.path.join(HERE, 'md_out')
STATES = os.path.join(TOOLS, 'states')
os.makedirs(OUT, exist_ok=True)
os.makedirs(STATES, exist_ok=True)

romb = _rom = open(ROM, "rb").read()


def r16(a):
    return struct.unpack('>H', _rom[a:a + 2])[0]


def r32(a):
    return struct.unpack('>I', _rom[a:a + 4])[0]


def d(a, n=30):
    print(dis('md', a, n, show_bytes=True))


def shot(md):
    """current frame as a PIL image"""
    w, h, px = md.pixels()
    im = Image.new('RGB', (w, h))
    im.putdata([((p >> 16) & 255, (p >> 8) & 255, p & 255) for p in px])
    return im


def grid(images, cols=4, labels=None, path=None, scale=1):
    w = max(i.size[0] for i in images)
    h = max(i.size[1] for i in images)
    lh = 12 if labels else 0
    rows = (len(images) + cols - 1) // cols
    g = Image.new('RGB', (w * cols, (h + lh) * rows), (40, 40, 40))
    dr = ImageDraw.Draw(g)
    for n, im in enumerate(images):
        x = (n % cols) * w
        y = (n // cols) * (h + lh)
        g.paste(im, (x, y + lh))
        if labels:
            dr.text((x + 2, y), str(labels[n]), fill=(255, 255, 0))
    if scale != 1:
        g = g.resize((g.size[0] * scale, g.size[1] * scale), Image.NEAREST)
    if path:
        g.save(path)
    return g


def start_play(md, entry, variant=1, limit=3000):
    """The module's level select (start_level returns as the level's story card begins), then START tapped through the
    card until the level init ($1AA484) and the frames run until the gameplay loop ($1A8C16) runs: the level's first
    frame of play. Clears the breakpoints (and configures the module again: bp_clear drops its PC hooks too). Returns
    whether the level is in play."""
    if not md.start_level(entry, variant):
        return False
    md.bp_hits()   # (drop an old log)
    md.bp(0x1AA484)
    md.bp(0x1A8C16)
    init = play = False
    for f in range(limit):
        if not init:
            md.hold('START' if f % 20 < 3 else 0)
        md.mrun(1)
        for h in md.bp_hits():
            init |= h['pc'] == 0x1AA484
            play |= h['pc'] == 0x1A8C16
        if init:
            md.hold(0)
        if play:
            break
    md.hold(0)
    md.bp_clear()
    md.configure(*md.config)
    return play


def save_state(md, name):
    open(os.path.join(STATES, name + '.state'), 'wb').write(md.save())


def load_state(md, name):
    md.load(open(os.path.join(STATES, name + '.state'), 'rb').read())


def snap(md):
    return bytes(md.ram)


def diff(a, b, lo=0, hi=0x10000):
    return [(0xFF0000 + i, a[i], b[i]) for i in range(lo, hi) if a[i] != b[i]]
