"""Checks the port's achievement detectors for Aladdin (MD) (achievement_frame in src/game/md_aladdin.cpp) on frames
of real play, fed one RAM image per frame like the app does.

    python tools/re/md_ach.py
"""
from md_util import *
from mdt import PortAchievements

LEVEL_END, PAUSED = 0xFFF0E9, 0xFFF158


class Run:
    def __init__(self, md, run_counts=True, bits=0):
        self.md = md
        self.ach = PortAchievements('md', run_counts, bits, md.ram_bytes())
        self.events = []

    def frames(self, n, buttons=None):
        if buttons is not None:
            self.md.hold(buttons)
        for _ in range(n):
            self.md.mrun(1)
            ev = self.ach.step(self.md.ram_bytes())
            self.events += [e for e in ev if e[0] != 'progress']
        return self

    def unlocked(self):
        return [e[1] for e in self.events if e[0] == 'unlock']


def new_game(md, difficulty=1):
    """power-on -> title -> START (the game's own new game), as a player would"""
    md.configure(0)
    md.reset()
    md.run(40)
    md.set8(0xFF7E21, difficulty)
    r = Run(md)
    for _ in range(60):
        r.frames(3, 'START').frames(17, 0)
        if md.info & 1:
            break
    return r


def finish_level(r, skip=False):
    """end the level: the game's level-end flag (as an exit / boss does), or the pause screen's skip code"""
    md = r.md
    if skip:
        r.frames(4, 'START').frames(10, 0)
        for b in 'ABBAABBA':
            r.frames(3, b).frames(4, 0)
        r.frames(120)
    else:
        md.set8(LEVEL_END, 0x20)
        r.frames(60)


if __name__ == '__main__':
    md = ModuleMD(ROM, 'md')
    r = new_game(md)
    print('new game: run bits %x, level %d' % (r.ach.run_bits(), md.ram8(0xFF7E26)))
    r.frames(120, 'LEFT')
    finish_level(r)
    print('market cleared without a scratch / apple:', r.unlocked())
    # the next levels until the desert
    for _ in range(40):
        r.frames(3, 'START').frames(17, 0)
        if md.info & 1 and md.ram8(0xFF7E26) == 3:
            break
    r.frames(60, 0)
    r.frames(40, 'A').frames(40, 0)
    finish_level(r, skip=True)
    print('desert skipped with the code (no award, run marked): bits %x, unlocked %s' % (r.ach.run_bits(), r.unlocked()))

    # Jafar's Palace from a run that counts (as if played from a new game)
    md.configure(0)
    md.start_level(12, 1)
    r = Run(md, True, 1)
    r.frames(60)
    finish_level(r)
    print('Jafar\'s Palace finished in a clean run:', r.unlocked())
    md.start_level(12, 2)
    r = Run(md, True, 1 | 2 | 4)
    r.frames(60)
    finish_level(r)
    print('... on Difficult with a life lost:', r.unlocked())
    # a hit, then the end of the level: no Not a Scratch
    md.start_level(0, 1)
    r = Run(md)
    r.ach.level_select()   # (as the app does after a level select start)
    r.frames(400, 'RIGHT')
    print('market health after the coals: %d' % md.ram8(0xFFEFFA))
    finish_level(r)
    print('market cleared after a hit (no apple thrown):', r.unlocked())
