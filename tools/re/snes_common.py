"""Shared helpers for the SNES Aladdin reverse-engineering scripts (tools/re/snes_*.py).

    import sys; sys.path.insert(0, r'K:\\projects\\Aladdin\\tools\\re')
    from snes_common import *
    s = SNES()                    # the ROM in the project root, build/tools/snestool.dll
    s.run(600); grab(s)           # PIL image of the current frame
    sheet([img, ...], path)       # contact sheet
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
os.environ.setdefault('SNESTOOL_DLL', os.path.join(ROOT, 'build', 'tools', 'snestool.dll'))

from snt import SNES, ROM, BTN, buttons  # noqa: E402

QA = os.path.join(ROOT, 'tools', 'qa', 'snes')
STATES = os.path.join(ROOT, 'tools', 'states')
os.makedirs(QA, exist_ok=True)
os.makedirs(STATES, exist_ok=True)


def grab(s):
    """The current frame as a PIL image (the whole rendered width)."""
    from PIL import Image
    w, h, buf = s.pixels()
    img = Image.new('RGB', (w, h))
    px = img.load()
    data = list(buf)
    for y in range(h):
        row = y * w
        for x in range(w):
            v = data[row + x]
            px[x, y] = ((v >> 16) & 255, (v >> 8) & 255, v & 255)
    return img


def grab_np(s):
    import numpy as np
    w, h, buf = s.pixels()
    a = np.frombuffer(bytes(buf), dtype=np.uint32).reshape(h, w)
    rgb = np.stack([(a >> 16) & 255, (a >> 8) & 255, a & 255], axis=-1).astype(np.uint8)
    return rgb


def img(s):
    from PIL import Image
    return Image.fromarray(grab_np(s))


def sheet(images, path, cols=4, labels=None, scale=1):
    """Contact sheet of PIL images (labels drawn above each)."""
    from PIL import Image, ImageDraw
    if not images:
        return
    w = max(i.width for i in images) * scale
    h = max(i.height for i in images) * scale
    lh = 12 if labels else 0
    rows = (len(images) + cols - 1) // cols
    out = Image.new('RGB', (cols * (w + 4), rows * (h + 4 + lh)), (40, 40, 40))
    d = ImageDraw.Draw(out)
    for k, im in enumerate(images):
        x = (k % cols) * (w + 4)
        y = (k // cols) * (h + 4 + lh)
        if scale != 1:
            im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
        out.paste(im, (x, y + lh))
        if labels:
            d.text((x + 2, y), str(labels[k]), fill=(255, 255, 0))
    out.save(path)
    return path


def r16(s, a):
    return s.r16(a)


def wram16(s, a):
    a &= 0x1FFFF
    return s.wram[a] | (s.wram[a + 1] << 8)
