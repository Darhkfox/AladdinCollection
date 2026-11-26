"""Trailer helpers shared by every script: paths, the hidden app instances that record footage, script building.

Footage is recorded by the real app (AladdinCollection.exe --autotest <CAP> <script> --capture-size 1920x1080, its
window hidden). Each recording runs in its own copy of the exe (trailer_work/inst/<name>) with its own settings.ini
(and, when asked, covers\\ and junctions soundtracks\\ / msu\\ to the trailer's own copy of the packs in
trailer_work/packs), launched from the repository root so the ROMs are found. Output in trailer_work/cap:
<name>.mkv (lossless), <name>.pcm (48 kHz stereo s16), <name>.txt (w h frames fps), shots, marks.txt.
"""
import os, sys, shutil, subprocess, struct

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))              # the checkout (ROMs, build)
WORK = os.environ.get('TRAILER_WORK', r'K:\projects\Aladdin\trailer_work')
CAP = os.path.join(WORK, 'cap')
INST = os.path.join(WORK, 'inst')
SCRIPTS = os.path.join(WORK, 'scripts')
PACKS = os.path.join(WORK, 'packs')                                 # copies of the soundtrack packs (never dist\)
ART = os.path.join(WORK, 'art')                                     # our own covers / background (art.py)
BIN = os.path.join(ROOT, 'build', 'bin')
EXE = os.path.join(BIN, 'AladdinCollection.exe')
for d in (CAP, INST, SCRIPTS):
    os.makedirs(d, exist_ok=True)

os.environ.setdefault('MDTOOL_DLL', os.path.join(ROOT, 'build', 'tools', 'mdtool.dll'))
os.environ.setdefault('SNESTOOL_DLL', os.path.join(ROOT, 'build', 'tools', 'snestool.dll'))
os.environ.setdefault('SMSTOOL_DLL', os.path.join(ROOT, 'build', 'tools', 'smstool.dll'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

# widescreen setting (settings.ini [video] widescreen=) -> extra pixels per side, CRT pixels (the default) / square
WIDE = ['4:3', '16:10', '16:9', '2:1', '21:9']
EXT_CRT = {'md': [0, 32, 54, 80, 128], 'snes': [0, 30, 46, 68, 106], 'sms': [0, 6, 22, 40, 74]}
EXT_SQ = {'md': [0, 20, 40, 64, 108], 'snes': [0, 52, 72, 96, 140], 'sms': [0, 26, 44, 64, 102]}
BASE_W = {'md': 320, 'snes': 256, 'sms': 256}
BASE_H = {'md': 224, 'snes': 224, 'sms': 192}


def ext_of(game, wide, aspect=0):
    return (EXT_SQ if aspect == 1 else EXT_CRT)[game][wide]


# ---------------------------------------------------------------- settings.ini
def settings_ini(game='md', wide=2, video=None, general=None, launcher=None, audio=None, cheats=None, extra=None):
    v = {'fullscreen': 0, 'window_scale': 3, 'widescreen': wide, 'aspect': 0, 'hud_anchor': 1, 'filter': 1, 'color': 0,
         'show_fps': 0}
    v.update(video or {})
    g = {'last_game': game, 'fast_forward': 1, 'rewind': 1, 'achievements': 0}
    g.update(general or {})
    la = {'menu_style': 0, 'covers': 0, 'background': 0, 'cover_fill': 0}
    la.update(launcher or {})
    secs = [('video', v), ('audio', dict(audio or {})), ('general', g), ('launcher', la), ('updates', {'check': 0})]
    for gid, names in (cheats or {}).items():
        secs.append(('cheats.' + gid, {k: 1 for k in names}))
    for sec, kv in (extra or {}).items():
        secs.append((sec, kv))
    lines = []
    for sec, kv in secs:
        lines.append('[%s]' % sec)
        lines += ['%s=%s' % x for x in kv.items()]
    return '\n'.join(lines) + '\n'


# ---------------------------------------------------------------- instances
def _is_junction(p):
    try:
        return os.path.isjunction(p) or os.path.islink(p)
    except OSError:
        return False


def remove_junctions(inst):
    """the instance's soundtracks / msu junctions: only the link itself goes (os.rmdir never empties a target)"""
    for sub in ('soundtracks', 'msu'):
        p = os.path.join(inst, sub)
        if _is_junction(p):
            os.rmdir(p)


def clear_instance(inst):
    if not os.path.exists(inst):
        return
    remove_junctions(inst)
    for sub in ('soundtracks', 'msu'):
        assert not _is_junction(os.path.join(inst, sub)), 'junction left in ' + inst
    shutil.rmtree(inst)


def junction(link, target):
    assert os.path.isdir(target), target
    subprocess.run(['cmd', '/c', 'mklink', '/J', link, target], check=True, capture_output=True)


def launch(name, ini_text, script_text, game=None, covers=None, packs=False, files=None, exe=EXE):
    """starts a hidden recording instance; returns the Popen. covers: a folder copied as covers\\;
    packs: junctions soundtracks\\ and msu\\ to trailer_work/packs (removed again by wait())."""
    inst = os.path.join(INST, name)
    clear_instance(inst)
    os.makedirs(inst)
    shutil.copy2(exe, os.path.join(inst, 'AladdinCollection.exe'))
    for f in ('librashader.dll',):
        if os.path.exists(os.path.join(BIN, f)):
            shutil.copy2(os.path.join(BIN, f), inst)
    if os.path.isdir(os.path.join(BIN, 'shaders')):
        shutil.copytree(os.path.join(BIN, 'shaders'), os.path.join(inst, 'shaders'))
    if covers:
        shutil.copytree(covers, os.path.join(inst, 'covers'))
    if packs:
        junction(os.path.join(inst, 'soundtracks'), os.path.join(PACKS, 'soundtracks'))
        junction(os.path.join(inst, 'msu'), os.path.join(PACKS, 'msu'))
    for fname, text in (files or {}).items():
        with open(os.path.join(inst, fname), 'w', encoding='utf-8') as f:
            f.write(text)
    with open(os.path.join(inst, 'settings.ini'), 'w') as f:
        f.write(ini_text)
    sp = os.path.join(SCRIPTS, name + '.txt')
    with open(sp, 'w') as f:
        f.write(script_text)
    argv = [os.path.join(inst, 'AladdinCollection.exe')]
    if game:
        argv += ['--game', game]
    argv += ['--autotest', CAP, sp, '--capture-size', '1920x1080']
    # (a script that can't be read makes the app open a normal fullscreen window: check it before every launch)
    assert os.path.isfile(sp) and os.path.getsize(sp) > 0, sp
    assert os.path.isfile(argv[0]), argv[0]
    print('launch', argv, flush=True)
    p = subprocess.Popen(argv, cwd=ROOT)
    p.inst = inst
    return p


def wait(p, timeout=3600):
    try:
        p.wait(timeout)
    except subprocess.TimeoutExpired:
        p.kill(); p.wait()          # (only our own process, by its handle)
        print('TIMEOUT', p.inst, flush=True)
    remove_junctions(p.inst)
    return p.returncode


def run_all(jobs, parallel=4):
    """jobs: [(name, callable returning a Popen)] run a few at a time"""
    running = []
    for name, start in jobs:
        running.append((name, start()))
        while len(running) >= parallel:
            n0, p0 = running.pop(0); rc = wait(p0); print(n0, 'done', rc, flush=True)
    for n0, p0 in running:
        rc = wait(p0); print(n0, 'done', rc, flush=True)


# ---------------------------------------------------------------- scripts
GAP = 14


class S:
    """an autotest script: one line per (frame, command); f is the current frame"""
    def __init__(self, start=40):
        self.f = start; self.lines = []
    def wait(self, n): self.f += n; return self
    def at(self, f): self.f = f; return self
    def tap(self, key, n=1, gap=GAP):
        for _ in range(n):
            self.lines.append('%d tap %s' % (self.f, key)); self.f += gap
        return self
    def cmd(self, text, gap=0): self.lines.append('%d %s' % (self.f, text)); self.f += gap; return self
    def down(self, key): return self.cmd('down ' + key)
    def up(self, key): return self.cmd('up ' + key)
    def hold(self, key, n): self.down(key); self.f += n; self.up(key); return self
    def click(self, x, y, gap=30): return self.cmd('click %.4f,%.4f' % (x, y), gap)
    def text(self, end_pad=5):
        return '\n'.join(self.lines + ['%d quit' % (self.f + end_pad)]) + '\n'


# ---------------------------------------------------------------- app-format save states, pad streams
def app_state(core, game, ext, cheats=(), module=b''):
    """core state + the module's bookkeeping + its 4-byte length + the tag '<id>|<ext>|<cheats>' + its length byte"""
    tag = ('%s|%d|%s' % (game, ext, ''.join(c + ',' for c in cheats))).encode()
    return bytes(core) + bytes(module) + struct.pack('<I', len(module)) + tag + bytes([len(tag)])


def write_state(name, data):
    with open(os.path.join(CAP, name + '.state'), 'wb') as f:
        f.write(data)


def write_pads(name, pads):
    """pads: p1 per frame (uint16 p1, p2 = 0)"""
    with open(os.path.join(CAP, name + '.pads'), 'wb') as f:
        for p in pads:
            f.write(struct.pack('<HH', p & 0xFFFF, 0))


def fnv(data):
    h = 2166136261
    for b in bytes(data):
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def marks():
    out = {}
    p = os.path.join(CAP, 'marks.txt')
    if os.path.exists(p):
        for line in open(p):
            w = line.split()
            if 'ram' in w:
                out[w[0]] = w[w.index('ram') + 1]
    return out
