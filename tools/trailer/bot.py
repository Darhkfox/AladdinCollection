"""Trailer gameplay without cheats: a search finds inputs that play a clip cleanly (no hits, no lost lives, no
falls, the level and gameplay kept) and keep Aladdin moving, then the plan is replayed from the start state without
any loads (as the app will replay it), checked, and written to trailer_work/cap for the app's autotest "loadstate" /
"pads" commands:  <clip>__<k>.state (one per variant: width / Remove Slowdown / HUD), <clip>.pads (uint16 p1, p2
per frame), <clip>.json (frames, each variant's RAM fingerprint after the clip: the app's "mark" must give the same).

  python bot.py <clip> [<clip> ...]        (clips are defined in clips.py)

Same method as the Mickey & Donald trailers' bot: CH-frame chunks; every candidate move is tried from the chunk's
start and scored by how far it gets plus the best of a few short continuations; unsafe moves score nothing, moves
other than plain running pay a little; with no safe move the search backs up. A clip with several variants (e.g. 4:3,
16:9 and 21:9 for a side-by-side) is searched in lockstep: a move is safe only if it is safe in every variant.
"""
import os, sys, json, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from tb import CAP, write_state, write_pads, fnv  # noqa: E402
from games import GAMES  # noqa: E402
from clips import CLIPS, variants  # noqa: E402

CH = 20
PENALTY = 6          # px a move other than running must gain to be chosen

CANDS = [
    ('run',    [('R', 20)]),
    ('jump',   [('R+J', 10), ('R', 10)]),
    ('hjump',  [('R+J', 20)]),
    ('attack', [('R+T', 4), ('R', 16)]),
    ('jatk',   [('R+J', 8), ('R+T', 4), ('R', 8)]),
    ('rjump',  [('R+J', 6), ('R', 4), ('R+J', 10)]),
    ('bjump',  [('L', 8), ('R+J', 12)]),
    ('wait',   [('', 20)]),
    ('vjump',  [('J', 20)]),
    ('back',   [('L', 20)]),
]
SWORD = [('sword', [('R+S', 4), ('R', 16)]), ('jsword', [('R+J', 8), ('R+S', 4), ('R', 8)])]
LOOKS = [[('R', 40)], [('R+J', 14), ('R', 26)], [('R+J', 30), ('R', 10)], [('', 40)], [('R+T', 4), ('R', 36)]]
# staying alive (auto-scrolling rides, boss arenas): no progress to make, the first safe move wins
SURVIVE = [('wait', [('', 20)]), ('up', [('U', 20)]), ('down', [('D', 20)]), ('right', [('R', 20)]),
           ('left', [('L', 20)]), ('uright', [('U+R', 20)]), ('dright', [('D+R', 20)]), ('hop', [('J', 8), ('', 12)]),
           ('jump', [('J', 20)]), ('ljump', [('L+J', 20)]), ('rjump', [('R+J', 20)])]
SURVIVE_LOOKS = [[('', 40)], [('U', 40)], [('D', 40)], [('J', 20), ('', 20)], [('L', 40)], [('R', 40)]]


class Multi:
    """the clip's variants run in lockstep"""
    def __init__(self, c):
        self.vs = [GAMES[c['game']](**v) for v in variants(c)]
    def pad(self, spec): return self.vs[0].pad(spec)
    def start(self, level, variant):
        return all(g.start(level, variant) for g in self.vs)
    def hold(self, p):
        for g in self.vs: g.hold(p)
    def run(self, n):
        for g in self.vs: g.run(n)
    def save(self): return [g.save() for g in self.vs]
    def load(self, st):
        for g, s in zip(self.vs, st): g.load(s)
    def status(self): return [g.status() for g in self.vs]


def ok1(a, b):
    return b['hp'] >= a['hp'] and b['lives'] >= a['lives'] and not b['bad'] and b['room'] == a['room']


def ok(a, b):
    return all(ok1(x, y) for x, y in zip(a, b))


def mirror(seq, c):
    if c.get('dir', 'right') == 'right': return seq
    sw = lambda b: '+'.join({'R': 'L', 'L': 'R'}.get(k, k) for k in b.split('+')) if b else b
    return [(sw(b), n) for b, n in seq]


def progress(s, c):
    if c.get('survive'): return 0
    return s[0]['x'] if c.get('dir', 'right') == 'right' else -s[0]['x']


def run_seq(g, seq):
    for btn, n in seq:
        g.hold(g.pad(btn)); g.run(n)


def cand_list(c):
    if c.get('survive'): return SURVIVE, SURVIVE_LOOKS
    cands = CANDS + (SWORD if c['game'] == 'md' else [])
    return cands, LOOKS


def search_from(g, c, name, state, frame0, total):
    cl, ll = cand_list(c)
    cands = [(k, mirror(seq, c)) for k, seq in cl]
    looks = [mirror(l, c) for l in ll]
    path, tried, states, scores = [], [None], [state], []
    backs = 0
    while frame0 + len(path) * CH < total:
        d = len(path)
        if tried[d] is None:
            res = []
            for k, (cname, seq) in enumerate(cands):
                g.load(states[d])
                before = g.status()
                run_seq(g, seq)
                after = g.status()
                if not ok(before, after): continue
                mid = g.save()
                best = None
                for look in looks:
                    g.load(mid)
                    run_seq(g, look)
                    s = g.status()
                    if ok(after, s) and (best is None or progress(s, c) > best): best = progress(s, c)
                if best is None: continue
                res.append((max(best, progress(after, c)) - (PENALTY if k else 0), -k, k, mid, progress(after, c)))
            res.sort(key=lambda r: (r[0], r[1]), reverse=True)
            tried[d] = res
        if not tried[d]:
            if d == 0: print(name, 'no clean path at frame', frame0, flush=True); return None
            path.pop(); tried.pop(); states.pop(); scores.pop()
            tried[-1] = tried[-1][1:]
            backs += 1
            if backs > 600: print(name, 'too many backtracks', flush=True); return None
            continue
        score, _, k, mid, x = tried[d][0]
        path.append(k); states.append(mid); scores.append(x); tried.append(None)
    print(name, 'searched frames %d-%d: %d chunks, %d backtracks, progress %s -> %s' % (
        frame0, total, len(path), backs, scores[0] if scores else '-', scores[-1] if scores else '-'), flush=True)
    seq = []
    for k in path: seq += cands[k][1]
    return seq


def prefix(seq, frames):
    out, f = [], 0
    for btn, n in seq:
        if f >= frames: break
        n = min(n, frames - f)
        out.append((btn, n)); f += n
    return out


def replay(g, start, seq, check=True):
    """the plan from the start state without loads: the first chunk that goes wrong (or None), states per chunk"""
    g.load(start)
    f, bad, saved = 0, None, {0: g.save()}
    last = g.status()
    for btn, n in seq:
        run_seq(g, [(btn, n)]); f += n
        if f % CH == 0:
            s = g.status()
            if check and not ok(last, s): bad = f // CH - 1; break
            last = s
            saved[f // CH] = g.save()
    return bad, saved


def total_frames(c):
    if c.get('rewind'): return c.get('pre', 0) + c['rewind'][0]
    return c.get('pre', 0) + c['n'] * (4 if c.get('ff') else 1)


def plan(name, c):
    t0 = time.time()
    g = Multi(c)
    if not g.start(c['level'], c.get('variant', 1 if c['game'] == 'md' else 0)):
        print(name, 'START FAILED', flush=True); return None
    run_seq(g, c.get('intro', [('', 30)]))
    start = g.save()
    total = total_frames(c)
    seq = search_from(g, c, name, start, 0, total)
    for attempt in range(10):
        if seq is None: return None
        bad, saved = replay(g, start, seq)
        if bad is None: break
        back = max(0, bad - 2)
        print(name, 'replay went wrong in chunk %d; searching again from chunk %d' % (bad, back), flush=True)
        tail = search_from(g, c, name, saved[back], back * CH, total)
        if tail is None: return None
        seq = prefix(seq, back * CH) + tail
    else:
        print(name, 'still going wrong after 10 tries', flush=True); return None
    seq = prefix(seq, total)
    pads = []
    for btn, n in seq:
        pads += [g.pad(btn)] * n
    pads = pads[:total]
    g.load(start)
    s0 = g.status()
    for k, v in enumerate(g.vs):
        write_state('%s__%d' % (name, k), v.app_state(start[k]))
    write_pads(name, pads)
    replay(g, start, seq, check=False)
    s1 = g.status()
    hashes = ['%x' % fnv(v.ram()) for v in g.vs]
    json.dump(dict(clip=name, frames=len(pads), pre=c.get('pre', 0), hashes=hashes, seq=seq),
              open(os.path.join(CAP, name + '.json'), 'w'))
    print(name, 'plan ok: %d frames, hp %s -> %s, lives %s -> %s, x %s -> %s, ram %s  (%.0f s)' % (
        len(pads), s0[0]['hp'], s1[0]['hp'], s0[0]['lives'], s1[0]['lives'], s0[0]['x'], s1[0]['x'], hashes,
        time.time() - t0), flush=True)
    return seq


if __name__ == '__main__':
    for name in sys.argv[1:]:
        plan(name, CLIPS[name])
