"""Single-level widescreen screenshots through the port's module, with the frame's edge masks applied.

    python tools/re/md_shots.py <entry> <ext> [script]
script: comma-separated steps 'frames:buttons' (e.g. '60:,120:RIGHT,30:RIGHT+C'), a shot after each step
-> md_out/shot_<entry>_<ext>.png
"""
import sys
from md_util import *


def masked(md, im):
    i = md.info
    ml, mr = (i >> 8) & 255, (i >> 16) & 255
    if not (i & 1):
        return im   # (the app pillarboxes these)
    w, h = im.size
    d = ImageDraw.Draw(im)
    if ml:
        d.rectangle([0, 0, ml - 1, h], fill=(0, 0, 0))
    if mr:
        d.rectangle([w - mr, 0, w, h], fill=(0, 0, 0))
    return im


def run(entry, ext, script, variant=1, cheats=()):
    md = ModuleMD(ROM, 'md')
    md.configure(ext, True, cheats)
    start_play(md, entry, variant)
    ims, lab = [], []
    for step in script.split(','):
        f, b = step.split(':')
        md.hold(b)
        md.mrun(int(f))
        ims.append(masked(md, shot(md)))
        i = md.info
        lab.append('%s %s cam %d,%d g%d m%d/%d' % (f, b, md.ram16(0xFF7DF6), md.ram16(0xFF7DF8), i & 1, (i >> 8) & 255,
                                                (i >> 16) & 255))
    md.hold(0)
    return md, ims, lab


if __name__ == '__main__':
    entry, ext = int(sys.argv[1]), int(sys.argv[2])
    script = sys.argv[3] if len(sys.argv) > 3 else '60:,120:RIGHT,60:RIGHT+C,120:LEFT'
    md, ims, lab = run(entry, ext, script)
    grid(ims, 2, lab, OUT + '/shot_%d_%d.png' % (entry, ext))
