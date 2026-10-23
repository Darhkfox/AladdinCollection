"""Full-app widescreen review: AladdinCollection.exe --game sms --autotest, from the level-select states.

    python sms_app_test.py [--modes 1,2,3,4] [--stages 0,...,9] [--frames 600] [--rewind N] [--states sms_ls]

Converts tools/states/sms_ls<n>.state (raw core states, sms_wide_shots / start_level) into the app's save-state
format, writes a script that loads each, switches the widescreen mode, plays (Right held, jump every so often)
and takes screenshots; output in tools/re/sms_out/app/. --rewind N: after the play, hold R (the rewind hotkey) for N
frames with a screenshot every 10, then play on for 60 (needs rewind on and achievements off in a settings.ini next
to the exe).
"""
import argparse
import os
import subprocess
import sms_re as R

REPO = os.path.dirname(R.TOOLS)
EXE = os.path.join(REPO, 'build', 'bin', 'AladdinCollection.exe')
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
    ap.add_argument('--cheats', default='invincible,', help="cheats the states carry (need achievements off: a "
                                                            "settings.ini next to the exe with [general] achievements=0)")
    ap.add_argument('--out', default=OUT)
    ap.add_argument('--rewind', type=int, default=0)
    ap.add_argument('--states', default='sms_ls', help='tools/states/<this><stage>.state (e.g. sms_end: near the ends)')
    a = ap.parse_args()
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    lines = ['1 set hud_anchor=%d' % a.hud]
    fr = 10
    for st in [int(x) for x in a.stages.split(',')]:
        core = open(R.state_path('%s%d.state' % (a.states, st)), 'rb').read()
        open(os.path.join(out, 'ls%d.state' % st), 'wb').write(app_state(core, 0, a.cheats))
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
            if a.rewind:
                lines.append('%d down r' % fr)
                for k in range(0, a.rewind, 10):
                    lines.append('%d shot s%d_m%d_rw%03d' % (fr + k + 9, st, m, k))
                lines.append('%d up r' % (fr + a.rewind))
                lines.append('%d down right' % (fr + a.rewind + 1))
                for k in range(0, 60, 10):
                    lines.append('%d shot s%d_m%d_after%02d' % (fr + a.rewind + 2 + k, st, m, k))
                fr += a.rewind + 80
                lines.append('%d up right' % (fr - 10))
    lines.append('%d quit' % (fr + 10))
    script = os.path.join(out, 'script.txt')
    open(script, 'w').write('\n'.join(lines) + '\n')
    # (from the repository's root: the ROM is found there)
    subprocess.run([EXE, '--game', 'sms', '--autotest', out, script, '--capture-size', '1600x900'], timeout=1800, cwd=REPO)
    print('shots in', out)


if __name__ == '__main__':
    main()
