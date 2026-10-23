"""Runs the real app (build/bin/AladdinCollection.exe --autotest) on level states made with the port's module, for
screenshots with everything the app adds (edge masks, dimming, pillarboxing, HUD).

    python tools/re/md_apptest.py <outdir> <widescreen modes, e.g. 2,4> [entries] [hold frames]

States: start_level(entry) at ext 0 through mdtool, saved in the app's format (core state, module data, tag).
Script per entry: load the state, then for each widescreen mode a shot at once, after walking right and after
walking back left. Shots: <outdir>/e<entry>_w<mode>_<n>.png
"""
import os
import subprocess
import sys
from md_util import *

EXE = os.path.join(ROOT, 'build', 'bin', 'AladdinCollection.exe')


def app_state(md, ext=0, cheats=()):
    core = md.save()
    tag = ('md|%d|' % ext + ''.join(c + ',' for c in cheats)).encode()
    return core + b'\0\0\0\0' + tag + bytes([len(tag)])


def make_states(outdir, entries, variant=1, warm=60):
    md = ModuleMD(ROM, 'md')
    for e in entries:
        md.configure(0)
        md.start_level(e, variant)
        md.mrun(warm)
        open(os.path.join(outdir, 'e%d.state' % e), 'wb').write(app_state(md))


def script(entries, modes, walk=90):
    out, f = [], 30
    for e in entries:
        out.append('%d loadstate e%d' % (f, e))
        f += 5
        for m in modes:
            out.append('%d set widescreen=%d' % (f, m))
            f += 20
            out.append('%d shot e%d_w%d_0' % (f, e, m))
            out.append('%d down right' % (f + 1))
            f += walk
            out.append('%d up right' % f)
            out.append('%d shot e%d_w%d_1' % (f, e, m))
            out.append('%d down left' % (f + 1))
            f += walk
            out.append('%d up left' % f)
            out.append('%d shot e%d_w%d_2' % (f, e, m))
            f += 5
    out.append('%d quit' % (f + 10))
    return '\n'.join(out) + '\n'


if __name__ == '__main__':
    outdir = os.path.abspath(sys.argv[1])
    os.makedirs(outdir, exist_ok=True)
    modes = [int(m) for m in sys.argv[2].split(',')]
    entries = [int(e) for e in sys.argv[3].split(',')] if len(sys.argv) > 3 else list(range(13))
    walk = int(sys.argv[4]) if len(sys.argv) > 4 else 90
    make_states(outdir, entries)
    sp = os.path.join(outdir, 'script.txt')
    open(sp, 'w').write(script(entries, modes, walk))
    subprocess.run([EXE, '--game', 'md', '--autotest', outdir, sp, '--capture-size', '1600x900'], cwd=ROOT, timeout=900)
    shots = sorted(p for p in os.listdir(outdir) if p.endswith('.png') and not p.startswith('grid'))
    ims = [Image.open(os.path.join(outdir, p)).resize((640, 360)) for p in shots]
    for k in range(0, len(ims), 24):
        grid(ims[k:k + 24], 3, shots[k:k + 24], os.path.join(outdir, 'grid%d.png' % (k // 24)))
