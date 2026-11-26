"""Abu's bonus rounds wide (docs/re_md_wide.md section 6b): the rounds run on the 4:3 game's ROM, so their RAM must be
the 4:3 game's at every width.

    python tools/re/md_wide_bonus.py [ext ...] [--seeds 1,2] [--cheats invincible,...] [--rewind]

* From the round's story card (the level select's start_level, seeded random LEFT / RIGHT / START): 68000 and Z80 RAM
  at each width against widescreen off, every frame, through the round to the next level's story (which runs on the
  widescreen ROM again: the first difference there is the next level's own).
* From a save state made at 4:3 inside the round (start_play + 60 frames), loaded as the app does at each width.
* --rewind: a state (the core's and the module's part) every 2 frames, then from the end back to the start each loaded,
  one frame run with the same input, its picture against the forward run's (the module's extra sprite pieces in the
  sides come back with the state)."""
import argparse
import random
from md_util import *

ROUNDS = (1, 6)   # level select entries: Abu in Agrabah, Abu in the Cave
ROUND_LEVELS = (2, 6)


def story_run(entry, ext, seed, cheats, frames=2600):
    m = ModuleMD(ROM, 'md')
    m.configure(0, True, cheats)
    m.start_level(entry, 1)
    m.configure(ext, True, cheats)
    rnd = random.Random(seed)
    ram, lv, hold = [], [], 0
    for f in range(frames):
        if f % 12 == 0:
            hold = rnd.choice(['LEFT', 'RIGHT', 0, 'LEFT', 'RIGHT', 'START' if f < 600 and f % 48 == 0 else 0])
        m.hold(hold)
        m.mrun(1)
        ram.append(bytes(m.ram) + bytes(m.zram))
        lv.append(m.ram8(0xFF7E26))
    return ram, lv


def state_run(st, ext, cheats, frames=2000, seed=3):
    m = ModuleMD(ROM, 'md')
    m.configure(0, True, cheats)
    m.load_full(st)
    m.configure(ext, True, cheats)
    rnd = random.Random(seed)
    ram, lv, hold = [], [], 0
    for f in range(frames):
        if f % 10 == 0:
            hold = rnd.choice(['LEFT', 'RIGHT', 0])
        m.hold(hold)
        m.mrun(1)
        ram.append(bytes(m.ram) + bytes(m.zram))
        lv.append(m.ram8(0xFF7E26))
    return ram, lv


def report(what, ext, base, lv, r):
    last = max(f for f in range(len(lv)) if lv[f] in ROUND_LEVELS)
    diff = [f for f in range(len(r)) if r[f] != base[f]]
    inside = [f for f in diff if f <= last]
    print('%s ext %d: frames 0-%d in the round, RAM differs on %d of them; first difference %s%s' % (
        what, ext, last, len(inside), diff[0] if diff else '-', ' (level %d)' % lv[diff[0]] if diff else ''))
    return not inside


def rewind(entry, ext, cheats, frames=900):
    m = ModuleMD(ROM, 'md')
    m.configure(ext, True, cheats)
    start_play(m, entry)
    m.mrun(20)
    rnd = random.Random(11)
    saved, hold = [], 0
    for f in range(frames):
        if f % 12 == 0:
            hold = rnd.choice(['LEFT', 'RIGHT', 0])
        st = m.save_full() if f % 2 == 0 else None
        m.hold(hold)
        m.mrun(1)
        if st:
            saved.append((st, hold, m.pixels()))
    bad = 0
    for st, hold, pic in reversed(saved):
        m.load_full(st)
        m.hold(hold)
        m.mrun(1)
        bad += m.pixels() != pic
    print('rewind entry %d ext %d: %d states, %d rewound frames differ' % (entry, ext, len(saved), bad))
    return bad == 0


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('ext', nargs='*', type=int, default=[54, 128])
    ap.add_argument('--seeds', default='1,2')
    ap.add_argument('--cheats', default='')
    ap.add_argument('--rewind', action='store_true')
    a = ap.parse_args()
    cheats = [c for c in a.cheats.split(',') if c]
    ok = True
    for entry in ROUNDS:
        if a.rewind:
            for e in a.ext:
                ok &= rewind(entry, e, cheats)
            continue
        for seed in [int(s) for s in a.seeds.split(',')]:
            base, lv = story_run(entry, 0, seed, cheats)
            for e in a.ext:
                ok &= report('entry %d seed %d from the story card,' % (entry, seed), e, base, lv,
                             story_run(entry, e, seed, cheats)[0])
        m = ModuleMD(ROM, 'md')
        m.configure(0, True, cheats)
        start_play(m, entry)
        m.mrun(60)
        st = m.save_full()
        base, lv = state_run(st, 0, cheats)
        for e in a.ext:
            ok &= report('entry %d from a 4:3 state,' % entry, e, base, lv, state_run(st, e, cheats)[0])
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)
