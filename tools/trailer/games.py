"""Headless sessions of the three games that match the app (the tool DLLs + the real game modules), for bot.py.

Each adapter: start(level, variant), hold(pad), run(n), save() / load(st), status(), ram(), app_state(st) (the app's
save-state format, see tb.app_state), pad('RIGHT+J') with J (jump), T (throw) and S (sword, Mega Drive) mapped to
the console's buttons. Configured as the app configures a game: the width's extra pixels, HUD re-anchored, the
clip's cheats, no sprites-per-line limit (Remove Sprite Flicker, on by default) and, on the SNES, Remove Slowdown
(CPU turbo 4, on by default).
"""
import os
from tb import app_state  # noqa: F401  (also sets the DLL paths)


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


class MDGame:
    gid = 'md'
    KEYS = {'J': 'C', 'T': 'A', 'S': 'B', 'R': 'RIGHT', 'L': 'LEFT', 'U': 'UP', 'D': 'DOWN'}

    def __init__(self, ext=0, cheats=(), hud=True, rom=None, **_):
        from mdt import ModuleMD, ROM, BTN
        self.BTN = BTN
        self.m = ModuleMD(rom or ROM, 'md')
        self.ext, self.cheats, self.hud = ext, tuple(cheats), hud
        self.gameplay = False
        self.reconf()

    def reconf(self):
        self.m.configure(self.ext, self.hud, self.cheats)
        self.m.set_ext(self.ext, False)

    def pad(self, spec):
        return sum(self.BTN[self.KEYS.get(k, k)] for k in spec.split('+') if k)

    def start(self, level, variant=1, limit=3000):
        """the module's level select, START through the story card until the gameplay loop runs ($1A8C16)"""
        m = self.m
        if not m.start_level(level, variant):
            return False
        m.bp_hits(); m.bp(0x1AA484); m.bp(0x1A8C16)
        init = play = False
        for f in range(limit):
            if not init:
                m.hold(0x80 if f % 20 < 3 else 0)
            m.mrun(1)
            for h in m.bp_hits():
                init |= h['pc'] == 0x1AA484
                play |= h['pc'] == 0x1A8C16
            if init:
                m.hold(0)
            if play:
                break
        m.hold(0)
        m.bp_clear()
        self.reconf()
        return play

    def hold(self, p): self.m.hold(p)

    def run(self, n):
        self.gameplay = self.m.mrun(n)['gameplay']

    def save(self): return self.m.save_full()

    def load(self, st):
        self.m.load_full(st)
        self.m.set_ext(self.ext, False)

    def status(self):
        m = self.m
        return dict(x=s16(m.ram16(0x7E02)), y=s16(m.ram16(0x7E04)), hp=m.ram8(0xEFFA), lives=m.ram8(0x7E3C),
                    bad=0 if self.gameplay else 1, room=m.ram8(0x7E26))

    def ram(self): return bytes(self.m.ram)

    def app_state(self, st):
        core, mod = st
        return app_state(core, 'md', self.ext, self.cheats, mod)


class SNESGame:
    gid = 'snes'
    KEYS = {'J': 'B', 'T': 'Y', 'R': 'RIGHT', 'L': 'LEFT', 'U': 'UP', 'D': 'DOWN'}

    def __init__(self, ext=0, cheats=(), hud=True, turbo=4, **_):
        from snt import SNES, BTN
        self.BTN = BTN
        self.m = SNES()
        self.m.module('snes')
        self.ext, self.cheats, self.hud, self.turbo = ext, tuple(cheats), hud, turbo
        self.m.turbo(turbo)
        self.reconf()

    def reconf(self):
        self.m.configure(self.ext, self.hud, ','.join(self.cheats))

    def pad(self, spec):
        return sum(self.BTN[self.KEYS.get(k, k)] for k in spec.split('+') if k)

    def start(self, level, variant=0):
        return self.m.start_level(level, variant)

    def hold(self, p): self.m.hold(p)
    def run(self, n): self.m.run(n)

    def save(self):
        return self.m.save()

    def load(self, st):
        """as the app loads a state with no module data (the tool has no save of the module's part)"""
        self.reconf()
        self.m.load(st)
        self.m.load_ext_empty() if hasattr(self.m, 'load_ext_empty') else _snes_load_ext(self.m, b'')

    def status(self):
        m = self.m
        info = m.info()
        return dict(x=m.r16(0x1773), y=m.r16(0x1777), hp=m.r8(0x367), lives=m.r8(0x364),
                    bad=(m.r8(0x347) & 0x82) | (0 if info['gameplay'] else 1), room=(m.r8(0x35C), m.r8(0x35D)))

    def ram(self): return bytes(self.m.wram_bytes())

    def app_state(self, st):
        return app_state(st, 'snes', self.ext, self.cheats, b'')


def _snes_load_ext(s, data):
    import ctypes as C
    import snt
    buf = (C.c_uint8 * max(1, len(data)))()
    snt._dll.snt_module_load_ext(s.h, buf, len(data))


class SMSGame:
    gid = 'sms'
    KEYS = {'J': '2', 'T': '1', 'R': 'RIGHT', 'L': 'LEFT', 'U': 'UP', 'D': 'DOWN'}

    def __init__(self, ext=0, cheats=(), hud=True, **_):
        from smst import SMS, BTN
        self.BTN = BTN
        self.m = SMS()
        self.m.module('sms')
        self.ext, self.cheats, self.hud = ext, tuple(cheats), hud
        self.gameplay = False
        self.reconf()

    def reconf(self):
        self.m.configure(self.ext, self.hud, ','.join(self.cheats))
        self.m.set_ext(self.ext, False)

    def pad(self, spec):
        return sum(self.BTN[self.KEYS.get(k, k)] for k in spec.split('+') if k)

    def start(self, level, variant=0):
        return self.m.start_level(level, variant)

    def hold(self, p): self.m.hold(p)

    def run(self, n):
        self.gameplay = self.m.mframes(n)['gameplay']

    def save(self): return (self.m.save(), self.m.save_ext())

    def load(self, st):
        core, mod = st
        self.reconf()
        self.m.load(core)
        self.m.load_ext(mod)
        self.m.set_ext(self.ext, False)

    def status(self):
        m = self.m
        return dict(x=m.ram16(0xC482) + m.ram8(0xC402), y=m.ram8(0xC400), hp=m.ram8(0xC116), lives=0,
                    bad=m.ram8(0xC438) | m.ram8(0xC461) | (0 if self.gameplay else 1),
                    room=(m.ram8(0xC105), m.ram8(0xC200)))

    def ram(self): return bytes(self.m.ram)

    def app_state(self, st):
        core, mod = st
        return app_state(core, 'sms', self.ext, self.cheats, mod)


GAMES = {'md': MDGame, 'snes': SNESGame, 'sms': SMSGame}
