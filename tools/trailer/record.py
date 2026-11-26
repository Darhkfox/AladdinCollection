"""Records trailer gameplay with the real app (hidden autotest window, 1920x1080):
  python record.py <clip> [...]     clips from clips.py, replayed from the bot's state + pads, plus each clip's takes
                                    (<clip>_<take>: the same inputs with other settings)
The game is started with --game, the bot's start state loaded and its pads replayed; the app's RAM fingerprint
after the replay (marks.txt) must equal the bot's for the take's variant (takes the bot didn't search in lockstep
are reported, not judged: their settings may change what happens).
"""
import os, sys, json
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from tb import CAP, S, settings_ini, launch, wait, marks  # noqa: E402
from clips import CLIPS, takes  # noqa: E402

L = 60                     # loop at which the bot's state is loaded (the game started with --game at power-on)


def gameplay_script(name, out, c, frames, state, extra=()):
    pre = c.get('pre', 0)
    s = S()
    s.at(L).cmd('loadstate ' + state).cmd('pads ' + name)
    for f, text in extra:
        s.at(L + f).cmd(text)
    s.at(L + pre).cmd('record ' + out)
    if c.get('ff'):
        s.down('tab'); s.at(L + pre + c['n']).up('tab')
        end = L + pre + c['n']
    elif c.get('rewind'):
        at, loops = c['rewind']
        s.at(L + pre + at).down('r'); s.at(L + pre + at + loops).up('r')
        end = L + pre + at
    else:
        end = L + frames
    s.at(end).cmd('mark %s_end' % out)
    s.at(L + pre + c['n']).cmd('stoprec')
    s.at(max(end, L + pre + c['n']) + 2)
    return s.text()


def jobs(name):
    """[(out, take settings, variant index or None)]"""
    c = CLIPS[name]
    out, k = [], 0
    for suffix, t in takes(c):
        searched = not suffix or suffix in c.get('lockstep', ())
        out.append((name + ('_' + suffix if suffix else ''), t, k if searched else None))
        if searched: k += 1
    return out


def start(name, out, t, vi):
    c = CLIPS[name]
    info = json.load(open(os.path.join(CAP, name + '.json')))
    video = dict(t.get('video', {}))
    video['widescreen'] = t['wide']
    if 'hud' in t: video['hud_anchor'] = t['hud']
    general = dict(t.get('general', {}))
    if 'turbo' in t: general['no_slowdown'] = 1 if t['turbo'] > 1 else 0
    audio = t.get('audio')
    state = '%s__%d' % (name, vi if vi is not None else 0)
    ini = settings_ini(c['game'], t['wide'], video=video, general=general, audio=audio,
                       cheats={c['game']: c['cheats']} if c.get('cheats') else None)
    return launch(out, ini, gameplay_script(name, out, c, info['frames'], state, t.get('script', ())),
                  game=c['game'], packs=bool(t.get('packs')))


def check(out, name, vi):
    info = json.load(open(os.path.join(CAP, name + '.json')))
    m = marks().get(out + '_end')
    tp = os.path.join(CAP, out + '.txt')
    t = open(tp).read().split() if os.path.exists(tp) else ['?'] * 3
    want = info['hashes'][vi] if vi is not None else None
    verdict = 'OK' if m == want else ('(not searched)' if want is None else 'MISMATCH')
    print('%-24s frames %s  ram app %s bot %s  %s' % (out, t[2], m, want, verdict), flush=True)
    return verdict


def record(names, parallel=4):
    running = []
    def reap():
        out, name, vi, p = running.pop(0); wait(p); check(out, name, vi)
    for name in names:
        for out, t, vi in jobs(name):
            running.append((out, name, vi, start(name, out, t, vi)))
            while len(running) >= parallel: reap()
    while running: reap()


if __name__ == '__main__':
    record(sys.argv[1:])
