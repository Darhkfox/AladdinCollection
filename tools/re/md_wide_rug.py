"""The Rug Ride in the widened view (Aladdin MD): the ride at 4:3 and wide, side by side with the same input.

    python tools/re/md_wide_rug.py [ext ...] [--seeds 1,2,3,4] [--cheats invincible] [--frames 30000] [--anchor]
                                   [--noise 0.06]
    python tools/re/md_wide_rug.py [ext ...] --states

For each seed one input track is made on the 4:3 run - an autopilot that keeps clear of the next rock pile (in view, or
the next the script will send) and else steers toward the next pickup of the trail ahead (the script's $FE entries lay
it along the way through), with random presses and spells of no steering mixed in, so it also crashes - and fed, frame
by frame, to the 4:3 run and to a run at every width given, all started from one state at the ride's first frame of
play (--noise: the share of frames that start a random spell; at 0 the autopilot flies the whole ride without a
crash). It runs until the ride is over (its end, or the third crash: the game's mercy) or for --frames. Per frame it
compares:
* RAM: the whole 68000 RAM and the Z80's, at the end of every frame (the ride runs on the 4:3 game's ROM: the same code
  to the cycle, the same lag frames);
* the ride as such, at the end of every gameplay-loop iteration (the loop's frame wait, $1A8CD2): camera, Aladdin and the
  carpet, speed / distance / script position, the random seed, crashes, lives, score ... and every object (type,
  position, frame, animation) - for the record, as the RAM covers them;
* the picture: the 4:3 area of the wide frame (screen x 0..319, frame columns 2E..2E+319 with the view slid by -E)
  against the 4:3 frame, on the frames shown wide; HUD unanchored (it then stays on the 4:3 area) unless --anchor, and
  no sprite limit at either width;
* pop-in: objects (not screen-fixed) that first appear left of the 4:3 area and more than 32 px inside the view
  (things the 4:3 game spawns off its screen that the wider view would show appearing);
* ghosts: every object the game despawns by its window ($50 px left of the camera) while it is still inside the wide
  view must go on as one of the module's ghosts (its type and place in the module's save extension the next frame), but
  one whose animation ends at its next step anyway;
* achievements: the port's checks (PortAchievements) on each run's RAM unlock the same, on the same frames.
"""
import argparse
import ctypes as C
import random
from concurrent.futures import ProcessPoolExecutor
import numpy as np
from md_util import *
from mdt import _dll, PortAchievements

OBJ, NOBJ, SIZE = 0xFF7E40, 32, 0x42
RIDE = 8                 # level select entry (kLevels) of the Rug Ride; level id 8
PC_ITER_END = 0x1A8CD2   # the gameplay loop's frame wait: one iteration's game logic done (the frame boundary itself
                         # falls anywhere in an iteration)
FIELDS = [               # (address, bytes, name)
    (0xFF7E28, 1, 'iter'), (0xFF7DF6, 2, 'camX'), (0xFF7DF8, 2, 'camY'), (0xFF7DFA, 2, 'alX scr'),
    (0xFF7DFC, 2, 'alY scr'), (0xFF7E02, 2, 'alX'), (0xFF7E04, 2, 'alY'), (0xFFF086, 2, 'speed'),
    (0xFFF088, 2, 'speed timer'), (0xFFF084, 2, 'distance'), (0xFFF12E, 4, 'script'), (0xFF7DEA, 4, 'rng'),
    (0xFFF0A2, 2, 'wave'), (0xFFF006, 1, 'crashes'), (0xFF7E3C, 1, 'lives'), (0xFF7E3F, 1, 'continues'),
    (0xFFEFFA, 1, 'health'), (0xFFEFE0, 2, 'apples'), (0xFFEFE2, 2, 'gems'), (0xFF7E29, 6, 'score'),
    (0xFFF0E9, 1, 'level over'), (0xFFF0E6, 1, 'dying'), (0xFFF0E7, 1, 'crash'), (0xFF7E26, 1, 'level'),
]


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def w16(ram, a):
    return (ram[a & 0xFFFF] << 8) | ram[(a + 1) & 0xFFFF]


def frame(md):
    """the frame as a (lines, width) array of 0xRRGGBB"""
    w, h = C.c_int(), C.c_int()
    _dll.mdt_frame(md.h, None, C.byref(w), C.byref(h))
    buf = np.zeros(w.value * h.value, dtype=np.uint32)
    _dll.mdt_frame(md.h, buf.ctypes.data_as(C.POINTER(C.c_uint32)), C.byref(w), C.byref(h))
    return buf.reshape(h.value, w.value)


def ride_state(ram):
    st = [bytes(ram[a & 0xFFFF:(a & 0xFFFF) + n]) for a, n, _ in FIELDS]
    for i in range(NOBJ):   # every object: type, position, flags, flips, frame, animation, movement script
        o = ram[0x7E40 + i * SIZE:0x7E40 + (i + 1) * SIZE]
        st.append(bytes(o[0:10]) + bytes(o[0x14:0x18]) + bytes(o[0x20:0x24]) + bytes(o[0x0A:0x0E]) + bytes(o[0x35:0x36]))
    return st


def state_names(k):
    return FIELDS[k][2] if k < len(FIELDS) else 'object %d' % (k - len(FIELDS))


NOISE = [0.06]   # the share of frames that start a random spell (--noise)


def autopilot(ram, rnd, hold):
    """the next input: away from the next rock ahead (objects $54: piles rising from the lava, y 136, or hanging from
    the roof, y 48 - the $F7 entries), else toward the treasure trail ahead (objects $42), with random spells mixed in"""
    if hold[0] > 0:
        hold[0] -= 1
        return hold[1]
    if rnd.random() < NOISE[0]:
        hold[0], hold[1] = rnd.randint(4, 40), rnd.choice([0, BTN['UP'], BTN['DOWN'], BTN['A'], BTN['B'], BTN['C'],
                                                            BTN['UP'] | BTN['C'], BTN['DOWN'] | BTN['B']])
        return hold[1]
    ax, ay, camy = w16(ram, 0x7E02), w16(ram, 0x7E04), w16(ram, 0x7DF8)
    rock = trail = None
    for i in range(2, NOBJ):
        a = 0x7E40 + i * SIZE
        x, y = w16(ram, a + 2), w16(ram, a + 4)
        if ram[a] == 0x54 and ax - 48 < x < ax + 260 and (rock is None or x < rock[0]):
            rock = (x, y)
        if ram[a] == 0x42 and x > ax + 8 and (trail is None or x < trail[0]):
            trail = (x, y)
    script = (ram[0xF12E] << 24 | ram[0xF12F] << 16 | ram[0xF130] << 8 | ram[0xF131]) & 0xFFFFFF
    ahead = [romb[script + 2 * k + 1] for k in range(4) if romb[script + 2 * k] == 0xF7]   # (rocks the script sends next)
    ry = rock[1] - camy - 256 if rock is not None else ahead[0] if ahead else None
    if ry is not None:   # (a pile from the lava: over it; from the roof: under it; one in mid-air: the nearer way)
        al = ay - camy - 256
        target = camy + 256 + (16 if ry > 120 or (ry >= 60 and al < ry) else 140)
    elif trail is not None:
        target = trail[1] + 24
    else:
        return 0
    return BTN['UP'] if ay > target + 3 else BTN['DOWN'] if ay < target - 3 else 0


def ghosts(md):
    """(type, x, y) of the module's ghosts (its save extension: version, count, then address + record each)"""
    _, ext = md.save_full()
    if len(ext) < 2:
        return set()
    out = set()
    for k in range(ext[1]):
        o = ext[2 + k * (4 + SIZE) + 4:2 + (k + 1) * (4 + SIZE)]
        out.add((o[0], w16(o, 2), w16(o, 4)))
    return out


class Run:
    def __init__(self, state, ext, cheats, anchor):
        self.ext = ext
        self.md = ModuleMD(ROM, 'md')
        self.md.load(state)
        self.md.configure(ext, anchor, cheats)
        self.md.set_ext(ext, False)          # (no sprite limit at 4:3 either: the pictures are compared)
        self.md.snap_at(PC_ITER_END)
        self.snaps, self.ram = self.md.snap()
        self.prev = self.ram                 # the iteration before
        self.ghosts = set()                  # the module's ghosts at the end of the frame before

    def step(self, buttons):
        """a frame; whether an iteration ended in it (then self.ram / self.prev: the last two iterations' RAM)"""
        self.md.hold(buttons)
        self.md.mrun(1)
        n, ram = self.md.snap()
        new = n != self.snaps
        if new:
            self.snaps, self.prev, self.ram = n, self.ram, ram
        return new, self.md.info & 1


KINDS = ('ram', 'zram', 'lag', 'state', 'pic', 'popin', 'ghost', 'ach')


def ends_next(ram, a):
    """the object's animation ends at its next step (its script at +$20: a frame, then $F6 00 - the end; no wait)"""
    p = (ram[a + 0x21] << 16 | ram[a + 0x22] << 8 | ram[a + 0x23])
    return not ram[a + 0x37] and p + 4 <= len(romb) and romb[p + 2] == 0xF6 and romb[p + 3] == 0


def run(job):
    seed, exts, cheats, frames, state, anchor, noise = job
    NOISE[0] = noise
    rnd = random.Random(seed)
    ref = Run(state, 0, cheats, False)
    wides = [Run(state, e, cheats, anchor) for e in exts]
    # the port's achievement checks on each run's RAM (Magic Carpet Ride: the ride's end, not by the third crash)
    ach = {id(r): PortAchievements('md') for r in [ref] + wides}
    unlocked = {id(r): [] for r in [ref] + wides}
    for r in [ref] + wides:
        ach[id(r)].level_select()
        ach[id(r)].step(r.md.ram_bytes())
    over = None
    hold = [0, 0]
    res = {e: {k: [0, []] for k in KINDS} for e in exts}
    for e in exts:
        res[e].update(shown=0, iters=0, despawns=0)

    def note(R, k, f, what):
        R[k][0] += 1
        if len(R[k][1]) < 4:
            R[k][1].append((f, what))

    f = 0
    for f in range(frames):
        b = autopilot(ref.md.ram_bytes(), rnd, hold)
        r_new, _ = ref.step(b)
        r_ram, r_zram = ref.md.ram_bytes(), bytes(ref.md.zram)
        unlocked[id(ref)] += [(f, e[1]) for e in ach[id(ref)].step(r_ram) if e[0] == 'unlock']
        r_pic = None
        for w in wides:
            R = res[w.ext]
            w_new, w_shown = w.step(b)
            ram = w.md.ram_bytes()
            unlocked[id(w)] += [(f, e[1]) for e in ach[id(w)].step(ram) if e[0] == 'unlock']
            if ram != r_ram:
                note(R, 'ram', f, [hex(0xFF0000 + i) for i in range(0x10000) if ram[i] != r_ram[i]][:6])
            if bytes(w.md.zram) != r_zram:
                note(R, 'zram', f, '')
            if w.snaps != ref.snaps or w_new != r_new:
                note(R, 'lag', f, (ref.snaps, w.snaps))
            elif r_new:
                R['iters'] += 1
                st0, st1 = ride_state(ref.ram), ride_state(w.ram)
                if st0 != st1:
                    note(R, 'state', f, [state_names(k) for k in range(len(st0)) if st0[k] != st1[k]])
            if w_shown:
                R['shown'] += 1
                if r_pic is None:
                    r_pic = frame(ref.md)
                E = w.ext
                bad = int(np.count_nonzero(frame(w.md)[:, 2 * E:2 * E + 320] != r_pic))
                if bad:
                    note(R, 'pic', f, '%d px' % bad)
            if w_new and w_shown and ram[0x7E26] == 8:
                E, s = w.ext, w.md.view()[0]
                cam, pcam = w16(w.ram, 0x7DF6), w16(w.prev, 0x7DF6)
                rel = lambda x, c: s16((x - c - 16) & 0xFFFF)   # screen x (16 bits: the camera wraps late in the ride)
                gl = None
                for i in range(2, NOBJ):
                    a = 0x7E40 + i * SIZE
                    t, pt = w.ram[a], w.prev[a]
                    if t and not pt and not (w.ram[a + 6] & 8):   # a new object: left of the 4:3 area, well inside?
                        x, y = w16(w.ram, a + 2), w16(w.ram, a + 4)
                        # (not one sent out by another there - sparks of a geyser: they come from it at 4:3 too)
                        parent = any(w.prev[0x7E40 + k * SIZE] and abs(s16((w16(w.prev, 0x7E42 + k * SIZE) - x) & 0xFFFF)) < 24
                                     and abs(s16((w16(w.prev, 0x7E44 + k * SIZE) - y) & 0xFFFF)) < 48 for k in range(1, NOBJ))
                        if -E + s + 32 < rel(x, cam) < -16 and not parent:
                            note(R, 'popin', f, ('%02x' % t, rel(x, cam)))
                    # despawned by the window on the left: $50 px left of the camera the object loop saw (as a signed
                    # distance: the game's unsigned test also takes objects sent in at the right edge once the camera
                    # has wrapped, and the 4:3 game shows them vanish there)
                    if pt and not (w.prev[a + 6] & 8) and (not t or w.ram[a + 2:a + 6] != w.prev[a + 2:a + 6]):
                        x = w16(w.prev, a + 2)
                        if rel(x, pcam) + 16 < -0x50 and w16(w.prev, a + 0x14) | w16(w.prev, a + 0x16):
                            if rel(x, cam) + 160 > -E + s:   # (any of it still in the view: pieces reach 160 px right)
                                R['despawns'] += 1
                                gl = (ghosts(w.md) | w.ghosts) if gl is None else gl
                                if (pt, x, w16(w.prev, a + 4)) not in gl and not ends_next(w.prev, a):
                                    note(R, 'ghost', f, ('%02x' % pt, rel(x, cam)))
            # (a ghost can end in the iteration it starts in, its animation's last step: look at the frame before too)
            w.ghosts = ghosts(w.md) if w.ext and w.md.ram8(0xFF7E26) == 8 else set()
        if over is None and (ref.md.ram8(0xFFF0E9) or ref.md.ram8(0xFFF005)):
            over = f
        if ref.md.ram8(0xFF7E26) != 8 or (over is not None and f > over + 400):   # (the Genie's wheel waits for a press)
            break
    for w in wides:
        if unlocked[id(w)] != unlocked[id(ref)]:
            note(res[w.ext], 'ach', f, (unlocked[id(ref)], unlocked[id(w)]))
    r = ref.md.ram
    return seed, f + 1, r[0xF006], (r[0xF12E] << 24 | r[0xF12F] << 16 | r[0xF130] << 8 | r[0xF131]), r[0xF087], res, \
        unlocked[id(ref)]


def start_state(cheats):
    md = ModuleMD(ROM, 'md')
    md.configure(0, False, cheats)
    start_play(md, RIDE, 1)
    return md.save()


def states(job):
    """Save states mid-ride (the core's and the module's part, as the app keeps them), each loaded into a fresh
    instance and run on with the same input: picture and RAM against the run they came from, frame by frame; then the
    same states without the module's part (as from an older version: no ghosts)."""
    ext, cheats, state, at, n = job
    NOISE[0] = 0.0
    rnd = random.Random(1)
    md = ModuleMD(ROM, 'md')
    md.load(state)
    md.configure(ext, True, cheats)
    hold, inputs, saved, frames = [0, 0], [], {}, {}
    for f in range(max(at) + n):
        b = autopilot(md.ram_bytes(), rnd, hold)
        inputs.append(b)
        if f in at:
            saved[f] = md.save_full()
        md.hold(b)
        md.mrun(1)
        frames[f] = (frame(md), md.ram_bytes())
    out = []
    for f, st in saved.items():
        for old in (False, True):
            m = ModuleMD(ROM, 'md')
            m.configure(ext, True, cheats)
            m.load_full((st[0], b'') if old else st)
            pic = ram = 0
            for k in range(f, f + n):
                m.hold(inputs[k])
                m.mrun(1)
                pic += int(np.count_nonzero(frame(m) != frames[k][0])) > 0
                ram += m.ram_bytes() != frames[k][1]
            out.append((f, len(st[1]), old, pic, ram))
    return ext, out


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('ext', nargs='*', type=int, default=[54, 128])
    ap.add_argument('--seeds', default='1,2,3,4')
    ap.add_argument('--cheats', default='')
    ap.add_argument('--frames', type=int, default=30000)
    ap.add_argument('--anchor', action='store_true')
    ap.add_argument('--noise', type=float, default=0.06)
    ap.add_argument('--states', action='store_true', help='the save-state check instead')
    a = ap.parse_args()
    cheats = tuple(c for c in a.cheats.split(',') if c)
    state = start_state(cheats)
    if a.states:
        with ProcessPoolExecutor(8) as ex:
            for ext, out in ex.map(states, [(e, cheats, state, (700, 2201, 3610, 4402, 5300), 90) for e in a.ext]):
                for f, size, old, pic, ram in out:
                    print('E=%3d: state at frame %d (module part %d bytes%s): %d of 90 frames after it differ in the picture, '
                          '%d in RAM' % (ext, f, size, ', left out' if old else '', pic, ram), flush=True)
        raise SystemExit
    jobs = [(int(s), a.ext, cheats, a.frames, state, a.anchor, a.noise) for s in a.seeds.split(',')]
    total = {}
    with ProcessPoolExecutor(min(len(jobs), 8)) as ex:
        for seed, n, crashes, script, speed, res, unlocked in ex.map(run, jobs):
            print('seed %d: %d frames, %d crashes, script at $%X%s, speed %d; achievements at 4:3: %s' % (
                seed, n, crashes, script, ' (the end)' if script >= 0x29A5 else '', speed, unlocked), flush=True)
            for e, R in res.items():
                fmt = lambda k: '%d%s' % (R[k][0], ' ' + str(R[k][1][:2]) if R[k][0] else '')
                print('  E=%3d: RAM differs in %s frames, Z80 RAM in %s; lag %s; ride state %s of %d iterations; 4:3 '
                      'area of the picture differs in %s of %d frames shown wide; pop-in in the extension %s; %d '
                      'despawned in view, without a ghost %s; achievements unlike 4:3 %s' % (
                          e, fmt('ram'), fmt('zram'), fmt('lag'), fmt('state'), R['iters'], fmt('pic'), R['shown'],
                          fmt('popin'), R['despawns'], fmt('ghost'), fmt('ach')), flush=True)
                t = total.setdefault(e, [0] * (len(KINDS) + 3))
                for i, k in enumerate(KINDS):
                    t[i] += R[k][0]
                t[-3] += R['shown']
                t[-2] += R['iters']
                t[-1] += R['despawns']
    for e, t in sorted(total.items()):
        print('E=%3d total: RAM differs %d, Z80 %d, lag %d, ride state %d of %d iterations, 4:3 area %d of %d frames, '
              'pop-in %d, %d despawned in view, %d without a ghost, achievements unlike 4:3 %d' % (
                  e, t[0], t[1], t[2], t[3], t[-2], t[4], t[-3], t[5], t[-1], t[6], t[7]))
