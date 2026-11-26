"""Contact sheets of recordings: python sheets.py out.png rec [rec ...] (k evenly spaced frames per recording, one row
each; SHEET_K=8 by default)"""
import os, sys, subprocess
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tb import CAP  # noqa: E402
W = 320
CAP_FPS = 59.9227


def frames(name, k=8):
    n = int(open(os.path.join(CAP, name + '.txt')).read().split()[2])
    out = []
    for j in range(k):
        t = (j + 0.5) * n / k / CAP_FPS
        raw = subprocess.run(['ffmpeg', '-v', 'error', '-ss', '%.3f' % t, '-i', os.path.join(CAP, name + '.mkv'),
                              '-frames:v', '1', '-vf', 'scale=%d:-1' % W, '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'],
                             capture_output=True).stdout
        h = len(raw) // (W * 3)
        out.append(Image.fromarray(np.frombuffer(raw, np.uint8).reshape(h, W, 3)) if h else Image.new('RGB', (W, 180)))
    return out


if __name__ == '__main__':
    out, names = sys.argv[1], sys.argv[2:]
    k = int(os.environ.get('SHEET_K', 8))
    rows = [(n, frames(n, k)) for n in names]
    h = rows[0][1][0].height
    sheet = Image.new('RGB', (W * k, (h + 16) * len(rows)), (30, 30, 30))
    d = ImageDraw.Draw(sheet)
    for r, (n, fr) in enumerate(rows):
        d.text((4, r * (h + 16) + 2), n, fill=(255, 255, 0))
        for j, im in enumerate(fr): sheet.paste(im.resize((W, h)), (j * W, r * (h + 16) + 16))
    sheet.save(out)
