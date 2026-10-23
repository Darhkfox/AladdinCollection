"""Runs the real app (build/bin/AladdinCollection.exe --autotest) on SNES level states, for screenshots with
everything the app adds (dimming, mirroring, pillarboxing, HUD):

    python tools/re/snes_apptest.py <outdir> <widescreen modes, e.g. 2,4> [levels] [walk frames]

Per level two states in the app's format (core state, no module data, tag `snes|0|`): its start (start_level at 4:3,
60 frames on) and, where tools/states/snes_end_<level>.state exists (snes_end_states.py), near its right end.
Script per state: load it, then for each widescreen mode a shot at once, after walking right and after walking back
left. Shots: <outdir>/l<level>_<start|end>_w<mode>_<n>.png, sheets <outdir>/grid<k>.png. Square pixels (21:9 = 140)
need `aspect=1` in build/bin/settings.ini ([video]); the script writes one with that (and achievements off) for the
run and removes it again if it wasn't there."""
import os
import subprocess
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa
from PIL import Image

EXE = os.path.join(ROOT, 'build', 'bin', 'AladdinCollection.exe')


def app_state(s):
    tag = b'snes|0|'
    return s.save() + b'\0\0\0\0' + tag + bytes([len(tag)])


def script(names, modes, walk):
    out, f = [], 30
    for n in names:
        out.append('%d loadstate %s' % (f, n))
        f += 5
        for m in modes:
            out.append('%d set widescreen=%d' % (f, m))
            f += 20
            out.append('%d shot %s_w%d_0' % (f, n, m))
            out.append('%d down right' % (f + 1))
            f += walk
            out.append('%d up right' % f)
            out.append('%d shot %s_w%d_1' % (f, n, m))
            out.append('%d down left' % (f + 1))
            f += walk
            out.append('%d up left' % f)
            out.append('%d shot %s_w%d_2' % (f, n, m))
            f += 5
    out.append('%d quit' % (f + 10))
    return '\n'.join(out) + '\n'


if __name__ == '__main__':
    outdir = os.path.abspath(sys.argv[1])
    os.makedirs(outdir, exist_ok=True)
    modes = [int(m) for m in sys.argv[2].split(',')]
    levels = [int(e) for e in sys.argv[3].split(',')] if len(sys.argv) > 3 else list(range(19))
    walk = int(sys.argv[4]) if len(sys.argv) > 4 else 90
    names = []
    for lv in levels:
        s = SNES()
        s.module('snes')
        s.configure(0)
        s.start_level(lv)
        s.run(60)
        open(os.path.join(outdir, 'l%d_start.state' % lv), 'wb').write(app_state(s))
        names.append('l%d_start' % lv)
        end = os.path.join(STATES, 'snes_end_%d.state' % lv)
        if os.path.exists(end):
            s.load_file(end)
            open(os.path.join(outdir, 'l%d_end.state' % lv), 'wb').write(app_state(s))
            names.append('l%d_end' % lv)
    sp = os.path.join(outdir, 'script.txt')
    open(sp, 'w').write(script(names, modes, walk))
    ini = os.path.join(os.path.dirname(EXE), 'settings.ini')
    had = os.path.exists(ini)
    if not had:
        open(ini, 'w').write('[video]\naspect=1\n[general]\nachievements=0\n[audio]\nachievements=0\n')
    try:
        subprocess.run([EXE, '--game', 'snes', '--autotest', outdir, sp, '--capture-size', '1280x720'], cwd=ROOT,
                       timeout=1800)
    finally:
        if not had:
            os.remove(ini)
    shots = sorted(p for p in os.listdir(outdir) if p.endswith('.png') and not p.startswith('grid'))
    ims = [Image.open(os.path.join(outdir, p)).resize((640, 360)) for p in shots]
    for k in range(0, len(ims), 12):
        print(sheet(ims[k:k + 12], os.path.join(outdir, 'grid%d.png' % (k // 12)), cols=3, labels=shots[k:k + 12]))
