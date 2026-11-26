"""The SNES lag frames (the level's main loop, $81:82CF, not started in a frame: the game slowed down) of a recorded
clip, replayed headlessly from its state + pads with Remove Slowdown off (turbo 1) and on (4), as in its takes:
  python lagframes.py <clip> [<take> <turbo>] ...
writes <rec>.dups.json (indices in the recording, i.e. after the clip's pre-roll) for the trailer's counter."""
import os, sys, json, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tb import CAP  # noqa: E402
from clips import CLIPS  # noqa: E402
from games import SNESGame  # noqa: E402

LOOP = 0x8182CF


def lag(name, turbo):
    c = CLIPS[name]
    data = open(os.path.join(CAP, name + '__0.state'), 'rb').read()
    n = data[-1]; tag = data[-1 - n:-1].decode()
    ext = int(tag.split('|')[1])
    core = data[:-1 - n - 4]
    g = SNESGame(ext=ext, turbo=turbo)
    g.load(core)
    raw = open(os.path.join(CAP, name + '.pads'), 'rb').read()
    pads = [struct.unpack_from('<HH', raw, 4 * i)[0] for i in range(len(raw) // 4)]
    s = g.m
    s.bp(LOOP); s.bp_hits()
    out = []
    for i, p in enumerate(pads):
        g.hold(p); g.run(1)
        if not s.bp_hits() and i >= c.get('pre', 0): out.append(i - c.get('pre', 0))
    s.bp_clear()
    return out


if __name__ == '__main__':
    name = sys.argv[1]
    for rec, turbo in ((name, 4), (name + '_orig', 1)):
        d = lag(name, turbo)
        json.dump(d, open(os.path.join(CAP, rec + '.dups.json'), 'w'))
        print(rec, 'turbo', turbo, len(d), 'lag frames', d[:30])
