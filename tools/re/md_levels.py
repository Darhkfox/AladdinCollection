"""Level select prototype for Aladdin (MD) and test states for every level (tools/states/md_lv<id>.state).

How the game enters a level: the title routine ($1B3B96) returns to the main entry, which loads level $FF7E26
($1A8B50: the title card $1B1486, the level init $1AA484, then the main loop $1A8C16). A finished level reads the
next one from the play-order script at $FFF572 (ROM $4082). Power-on sets level 1 and the script start once
($1AA41C); the title keeps them (the developers' level select on the debug screen relies on that). So the level
and the script position are set before START on the title.

    python tools/re/md_levels.py            # all levels, Normal
"""
from md_util import *

SEQ = 0x4082
ORDER = [1, 2, 3, 0, 4, 5, 6, 7, 8, 9, 0xA, 0xC, 0xB]
NAMES = {0: 'Agrabah Rooftops', 1: 'Agrabah Market', 2: 'Abu in Agrabah', 3: 'The Desert', 4: "Sultan's Dungeon",
         5: 'Cave of Wonders', 6: 'Abu in the Cave', 7: 'Escape from the Cave', 8: 'Rug Ride', 9: 'Inside the Lamp',
         0xA: "Sultan's Palace", 0xB: "Jafar's Palace", 0xC: "Jafar's Quarters"}


def script_after(level):
    """the play-order script position following `level`'s first entry (ROM $4082: cmd, level pairs)"""
    if level == 1:
        return SEQ
    a = SEQ
    while r16(a) != 0xFF:
        if r16(a + 2) == level:
            return a + 4
        a += 4
    raise KeyError(level)


def start(md, level, diff=1):
    md.reset()
    md.run(30)
    md.set8(0xFF7E26, level)
    md.set32(0xFFF572, script_after(level))
    md.set8(0xFF7E21, diff)
    md.bp_clear()
    md.bp(0x1AA484)
    for i in range(200):
        md.press('START', 3, 17)
        if md.bp_hits():
            break
    md.bp_clear()
    md.bp(0x1A8C16)
    for i in range(600):
        md.run(1)
        if md.bp_hits():
            break
    md.bp_clear()
    return md.frame


if __name__ == '__main__':
    md = MD()
    ims, lab = [], []
    for lv in ORDER:
        f = start(md, lv)
        md.run(2)
        save_state(md, 'md_lv%x' % lv)
        ims.append(shot(md)); lab.append('%x %s f%d' % (lv, NAMES[lv], f))
    grid(ims, 4, lab, OUT + '/md_levels.png')
