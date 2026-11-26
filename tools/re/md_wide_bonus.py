"""Abu's bonus rounds and the Genie's wheel wide (docs/re_md_wide.md sections 6a, 6b): they run on the 4:3 game's ROM,
so their RAM must be the 4:3 game's at every width.

    python tools/re/md_wide_bonus.py [ext ...] [--seeds 1,2] [--cheats invincible,...] [--rewind] [--wheel [state]]

* From the round's story card (the level select's start_level, seeded random LEFT / RIGHT / START): 68000 and Z80 RAM
  at each width against widescreen off, every frame, through the round to the next level's story (which runs on the
  widescreen ROM again: the first difference there is the next level's own).
* From a save state made at 4:3 inside the round (start_play + 60 frames), loaded as the app does at each width.
* --rewind: a state (the core's and the module's part) every 2 frames, then from the end back to the start each loaded,
  one frame run with the same input, its picture against the forward run's (the module's extra sprite pieces in the
  sides come back with the state).
* --wheel: the Genie's wheel from a natural entry - the Market (on the widescreen ROM) and the Rug Ride (on the 4:3
  game's ROM) with two Genie tokens, ended by the developers' level skip, C pressed every 150 frames (spin, stop) - and
  from an app save state given (core, module part, tag): per frame in the wheel (the stack mark $1A8E7E at $FFEFD0),
  the ROM (the clip word), shown wide, the results (tokens, the face it stops on, lives, apples, gems) and RAM against
  4:3."""
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


def wheel_run(ext, entry=None, state=None, frames=1400):
    if state is not None:
        m = ModuleMD(ROM, 'md')
        d = open(state, 'rb').read()
        n = d[-1]
        tag = d[-1 - n:-1].decode().split('|')
        end = len(d) - 1 - n
        ml = int.from_bytes(d[end - 4:end], 'little')
        cheats = [c for c in tag[2].split(',') if c]
        m.configure(int(tag[1]), True, cheats)
        m.load_full((d[:end - 4 - ml], d[end - 4 - ml:end - 4]))
        m.configure(ext, True, cheats)
    else:
        m = ModuleMD(ROM, 'md')
        m.configure(0, True, ['level_skip'])
        start_play(m, entry)
        m.set8(0xFFF003, 2)
        m.configure(ext, True, ['level_skip'])
        m.mrun(30)
        for b, n in (('START', 3), (0, 10), ('A', 3)):
            m.hold(b)
            m.mrun(n)
    out = []
    for f in range(frames):
        m.hold('C' if f % 150 in range(100, 104) else 0)
        i = m.mrun(1)
        rom = m.rom
        out.append((m.ram32(0xFFEFD0) == 0x1A8E7E and m.ram32(0xFFEFD4) == 0x1A8CCA,
                    (rom[0x1ABA8C] << 8 | rom[0x1ABA8D]) == 0x0443, i['gameplay'],
                    (m.ram8(0xFFF003), m.ram8(0xFF7E82), m.ram8(0xFFEFF4), m.ram16(0xFFEFE0), m.ram16(0xFFEFE2)),
                    bytes(m.ram)))
    return out


def wheel(exts, entry=None, state=None):
    base = wheel_run(0, entry, state)
    wf = [f for f, x in enumerate(base) if x[0]]
    ok = True
    for e in exts:
        r = wheel_run(e, entry, state)
        mine = [f for f, x in enumerate(r) if x[0]]
        differ = [f for f in mine if r[f][4] != base[f][4]]
        same = [x[3] for x in r] == [x[3] for x in base]
        print('wheel %s ext %d: in the wheel frames %s (%d); on the 4:3 ROM %d; shown wide %d; results as at 4:3 %s; '
              'RAM differs on %d of its frames%s' % (
                  'state' if state else 'entry %d' % entry, e, (mine[0], mine[-1]) if mine else '-', len(mine),
                  sum(1 for f in mine if r[f][1]), sum(1 for f in mine if r[f][2]), same, len(differ),
                  ' (%s)' % ','.join(str(f - mine[0]) for f in differ[:5]) if differ else ''))
        ok &= same and mine == wf and all(r[f][2] for f in mine)
    return ok


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('ext', nargs='*', type=int, default=[54, 128])
    ap.add_argument('--seeds', default='1,2')
    ap.add_argument('--cheats', default='')
    ap.add_argument('--rewind', action='store_true')
    ap.add_argument('--wheel', nargs='?', const='', default=None)
    a = ap.parse_args()
    cheats = [c for c in a.cheats.split(',') if c]
    ok = True
    if a.wheel is not None:
        for entry in (0, 8):
            ok &= wheel(a.ext, entry=entry)
        if a.wheel:
            ok &= wheel(a.ext, state=a.wheel)
        print('PASS' if ok else 'FAIL')
        sys.exit(0 if ok else 1)
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
