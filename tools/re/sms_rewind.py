"""Rewind check: the picture after loading a save state must be the one the run showed at that point.

    python sms_rewind.py [ext ...] [--stages 0,1,5,7,8] [--frames 1200] [--start 0] [--no-ext]
    python sms_rewind.py [ext ...] --scenes 0,1,20 [--ending]     (story scenes, mode $13; the ending, mode $12)

Plays each stage (sms_wide_shots.Player) and keeps a state (the core's and the module's save_ext, as the app's
rewind does) every 2 frames, with the picture of the frame after it. Then steps back as the app's rewind does: load
the state, run one frame (with the same input), compare its picture (sprites included) with the one the forward run drew - from the end
to the start, so each load follows a frame of a later time. --no-ext loads the core's state only (what a module
without save_ext would get), to see what the module's part is for. Writes the worst frame (forward | rewound |
differences) to sms_out/rewind_e<ext>_s<stage>.png. --scenes plays story scenes instead (from the title, the scene
set and mode $13, to the scene's end and through its fade-out into the next mode), --ending the ending (mode $12):
the widened story screens' extra sprites and hidden entries (StorySprites in the module) must come back as well.
"""
import argparse
import array
import sms_re as R
from PIL import Image
from sms_wide_shots import Player, drive, FREE


def img(px, w, h):
    im = Image.new('RGB', (w, h))
    im.putdata([((p >> 16) & 255, (p >> 8) & 255, p & 255) for p in px])
    return im


def run(ext, st, frames, start, use_ext):
    s = R.boot()
    s.module('sms')
    s.configure(ext, True, 'invincible')
    s.set_ext(ext)
    s.start_level(st)
    if st in FREE:
        drive(s, 2)
    p = Player(s, st, retry=False)
    for f in range(start):
        p.step()
    saved = []
    for f in range(frames):
        state = (s.save(), s.save_ext(), p.pad()) if f % 2 == 0 else None
        p.step()
        if not p.in_stage():
            break
        if state:
            px, w, h = s.pixels()
            saved.append((f, state, array.array('I', px)))
    bad = []
    worst = None
    for f, (core, ext_state, pad), want in reversed(saved):
        s.load(core)
        s.load_ext(ext_state if use_ext else b'')
        p.prep()   # (the same frame: the player's input and RAM writes)
        s.mframes(1, pad)
        px, w, h = s.pixels()
        px = array.array('I', px)
        d = sum(1 for a, b in zip(px, want) if a != b)
        if d:
            bad.append((f, d))
            if worst is None or d > worst[1]:
                worst = (f, d, px, want, w, h)
    if worst:
        f, d, px, want, w, h = worst
        sheet = Image.new('RGB', (w, h * 3))
        sheet.paste(img(want, w, h), (0, 0))
        sheet.paste(img(px, w, h), (0, h))
        sheet.paste(img([0xFF00FF if a != b else 0 for a, b in zip(px, want)], w, h), (0, 2 * h))
        sheet.save(R.OUT + '/rewind_e%d_s%d%s.png' % (ext, st, '' if use_ext else '_noext'))
    return len(saved), bad


def rewind(s, saved, use_ext, pad=None):
    bad = []
    for f, (core, ext_state), want in reversed(saved):
        s.load(core)
        s.load_ext(ext_state if use_ext else b'')
        s.mframes(1, pad)
        px, w, h = s.pixels()
        d = sum(1 for a, b in zip(array.array('I', px), want) if a != b)
        if d:
            bad.append((f, d))
    return bad


def run_story(ext, which, frames, use_ext):
    """a story scene (which = scene number) or the ending (which = 'end'), from the title"""
    s = R.boot()
    s.module('sms')
    s.configure(ext, True, '')
    s.set_ext(ext)
    for f in range(400):
        s.mframes(1, None)
    if which == 'end':
        s.set8(0xC105, 9)
        s.goto_mode(0x12)
    else:
        s.set8(0xC11F, which)
        s.goto_mode(0x13)
    saved = []
    left = None
    for f in range(frames):
        state = (s.save(), s.save_ext()) if f % 2 == 0 else None
        s.mframes(1, None)
        if state:
            px, w, h = s.pixels()
            saved.append((f, state, array.array('I', px)))
        # (on to the next mode's first frames: the scene's fade-out is part of it)
        if which != 'end' and left is None and (s.ram8(0xC200) != 0x13 or s.ram8(0xC11F) != which):
            left = f
        if left is not None and f > left + 80:
            break
    return len(saved), rewind(s, saved, use_ext)


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('ext', nargs='*', type=int, default=[74, 102])
    ap.add_argument('--stages', default='0,1,5,7,8')
    ap.add_argument('--frames', type=int, default=1200)
    ap.add_argument('--start', type=int, default=0)
    ap.add_argument('--no-ext', action='store_true')
    ap.add_argument('--scenes', default='', help='story scenes (hex) instead of stages')
    ap.add_argument('--ending', action='store_true', help='the ending (mode $12) instead of stages')
    a = ap.parse_args()
    if a.scenes or a.ending:
        which = [int(x, 16) for x in a.scenes.split(',') if x] + (['end'] if a.ending else [])
        for e in a.ext:
            for w in which:
                n, bad = run_story(e, w, a.frames, not a.no_ext)
                print('E=%d %s: %d states rewound, %d pictures differ %s' % (
                    e, 'ending' if w == 'end' else 'scene %02X' % w, n, len(bad), bad[:8]), flush=True)
        raise SystemExit
    for e in a.ext:
        for st in [int(x) for x in a.stages.split(',')]:
            n, bad = run(e, st, a.frames, a.start, not a.no_ext)
            print('E=%d stage %d: %d states rewound, %d pictures differ %s' % (e, st, n, len(bad), bad[:8]), flush=True)
