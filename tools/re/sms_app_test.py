"""Full-app widescreen review: AladdinCollection.exe --game sms --autotest, from the level-select states.

    python sms_app_test.py [--modes 1,2,3,4] [--stages 0,...,9] [--frames 600]

Converts tools/states/sms_ls<n>.state (raw core states, sms_wide_shots / start_level) into the app's save-state
format, writes a script that loads each, switches the widescreen mode, plays (Right held, jump every so often)
and takes screenshots; output in tools/re/sms_out/app/.
"""
import argparse
import os
import subprocess
import sms_re as R

EXE = os.path.join(os.path.dirname(R.TOOLS), 'build', 'bin', 'AladdinCollection.exe')
OUT = os.path.join(R.OUT, 'app')


def app_state(core, ext=0, cheats=''):
    tag = ('sms|%d|%s' % (ext, cheats)).encode()
    return core + b'\0\0\0\0' + tag + bytes([len(tag)])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--modes', default='4')
    ap.add_argument('--stages', default='0,1,2,3,4,5,6,7,8,9')
    ap.add_argument('--frames', type=int, default=600)
    ap.add_argument('--every', type=int, default=150)
    ap.add_argument('--hud', type=int, default=1)
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    lines = ['1 set hud_anchor=%d' % a.hud]
    fr = 10
    for st in [int(x) for x in a.stages.split(',')]:
        core = open(R.state_path('sms_ls%d.state' % st), 'rb').read()
        open(os.path.join(OUT, 'ls%d.state' % st), 'wb').write(app_state(core))
        for m in [int(x) for x in a.modes.split(',')]:
            lines.append('%d set widescreen=%d' % (fr, m))
            lines.append('%d loadstate ls%d' % (fr + 2, st))
            lines.append('%d down right' % (fr + 4))
            for k in range(a.frames // a.every):
                t = fr + 4 + k * a.every
                lines.append('%d tap z' % (t + 40))
                lines.append('%d shot s%d_m%d_%d' % (t + a.every - 1, st, m, k))
            fr += a.frames + 20
            lines.append('%d up right' % (fr - 10))
    lines.append('%d quit' % (fr + 10))
    script = os.path.join(OUT, 'script.txt')
    open(script, 'w').write('\n'.join(lines) + '\n')
    subprocess.run([EXE, '--game', 'sms', '--autotest', OUT, script, '--capture-size', '1600x900'], timeout=1200)
    print('shots in', OUT)


if __name__ == '__main__':
    main()
