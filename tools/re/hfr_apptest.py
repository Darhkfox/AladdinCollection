"""High frame rate in the real app (build/bin/AladdinCollection.exe --autotest), per game: a level state made with
the tool DLLs, played at 21:9 with the display taken as 240 Hz ("set refresh=240": 4 pictures a game frame).

  1. V-Sync on, one loop a refresh: eight consecutive refreshes are captured (the backbuffer, everything the app
     draws: masks, bars, the filter) and every loop's picture choice is logged (marks.txt "pick k/4": the in-betweens
     1, 2, 3, then the frame, 0, a frame late).
  2. Speed: "realtime 1" and V-Sync off (paced as on a G-SYNC / FreeSync display, a pass per picture): over 10 s the
     game frames run and the passes made, so the game's rate (60 = keeping up) and the pictures a second.

    python tools/re/hfr_apptest.py <outdir> [md,snes,sms] [--slang <preset relative to the exe>]

What a capture can't show: the refresh timing itself (when the display shows a picture) - the captures are the
pictures in the order they are presented.
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
EXE = os.path.join(ROOT, 'build', 'bin', 'AladdinCollection.exe')


def md_state():
    from mdt import ModuleMD, ROM
    import md_util
    md = ModuleMD(ROM, 'md')
    md.configure(0)
    md_util.start_play(md, 0)
    md.mrun(60)
    tag = b'md|0|'
    return md.save() + b'\0\0\0\0' + tag + bytes([len(tag)])


def snes_state():
    from snt import SNES
    s = SNES()
    s.module('snes')
    s.configure(0)
    s.start_level(0)
    s.run(60)
    tag = b'snes|0|'
    return s.save() + b'\0\0\0\0' + tag + bytes([len(tag)])


def sms_state():
    from smst import SMS
    s = SMS()
    s.module('sms')
    s.configure(0)
    s.start_level(0)
    s.mframes(60)
    tag = b'sms|0|'
    return s.save() + b'\0\0\0\0' + tag + bytes([len(tag)])


def script(slang):
    out = ['20 loadstate level', '22 set widescreen=4', '23 set refresh=240', '24 set high_frame_rate=1']
    if slang:
        out.append('25 set slang_preset=%s' % slang)
    out.append('30 down right')
    for f in range(200, 232):   # a mark per loop: the picture it shows
        out.append('%d mark l%d' % (f, f))
    for f in range(220, 228):
        out.append('%d shot hfr_%d' % (f, f))
    out += ['300 set vsync=0', '301 realtime 1', '320 mark speed0']
    return '\n'.join(out) + '\n'


def run(game, outdir, slang):
    d = os.path.join(outdir, game)
    os.makedirs(d, exist_ok=True)
    for f in os.listdir(d):
        if f.endswith('.png') or f == 'marks.txt':
            os.remove(os.path.join(d, f))
    open(os.path.join(d, 'level.state'), 'wb').write({'md': md_state, 'snes': snes_state, 'sms': sms_state}[game]())
    sp = os.path.join(d, 'script.txt')
    # (the speed run: marks every second of real time can't be scripted by loop, so loops are counted generously:
    # at 240 passes a second 10 s are 2400 loops)
    open(sp, 'w').write(script(slang) + '2720 mark speed1\n2730 quit\n')
    argv = [EXE, '--game', game, '--autotest', d, sp, '--capture-size', '1600x686']
    print('argv:', argv, 'cwd:', ROOT, flush=True)
    subprocess.run(argv, cwd=ROOT, timeout=900)
    marks = open(os.path.join(d, 'marks.txt')).read().split('\n')
    picks = []
    speed = {}
    for line in marks:
        p = line.split()
        if not p:
            continue
        if p[0].startswith('l'):
            picks.append(p[p.index('pick') + 1])
        else:
            speed[p[0]] = (float(p[1]), int(p[5]))   # (time, game frames run)
    print(game, 'picks per loop:', ' '.join(picks))
    seq = [x.split('/')[0] for x in picks]
    cyc = ''.join(seq)
    ok = '1230' in cyc and all(x.endswith('/4') for x in picks)
    if 'speed0' in speed and 'speed1' in speed:
        (t0, r0), (t1, r1) = speed['speed0'], speed['speed1']
        print('%s speed: %.1f game frames a second, %.0f passes a second (2400 passes in %.2f s)' %
              (game, (r1 - r0) / (t1 - t0), 2400 / (t1 - t0), t1 - t0))
    return ok


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('outdir')
    ap.add_argument('games', nargs='?', default='md,snes,sms')
    ap.add_argument('--slang', default='')
    a = ap.parse_args()
    out = os.path.abspath(a.outdir)
    res = {g: run(g, out, a.slang) for g in a.games.split(',')}
    print('picks in order:', res)
    sys.exit(0 if all(res.values()) else 1)
