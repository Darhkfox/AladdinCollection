"""Checks the port's cheats for Aladdin (MD) on real gameplay (through the module, build/tools/mdtool.dll).

    python tools/re/md_cheats.py
"""
from md_util import *

HEALTH, LIVES, APPLES, CONT = 0xFFEFFA, 0xFF7E3C, 0xFFEFE0, 0xFF7E3F


def level(cheats=(), entry=0, ext=0):
    md = ModuleMD(ROM, 'md')
    md.configure(ext, True, cheats)
    md.start_level(entry, 1)
    return md


def walk_into_coals(md):
    """Agrabah Market: the hot coals right of the start (and the guard behind them)"""
    md.hold('RIGHT')
    h = []
    for _ in range(40):
        md.mrun(10)
        h.append(md.ram8(HEALTH))
    md.hold(0)
    return h


def die(md, frames=600):
    """lose a life: the game's own death (health 0 at the next hit -> $FFF0E6)"""
    md.set8(0xFFF0E6, 0xFF)
    md.mrun(frames)


if __name__ == '__main__':
    for c in ((), ('inf_health',), ('invincible',), ('dev_cheat',)):
        md = level(c)
        print('%-16s health over the coals: %s' % (','.join(c) or 'none', walk_into_coals(md)))
    for c in ((), ('inf_lives',), ('dev_cheat',)):
        md = level(c)
        before = md.ram8(LIVES)
        die(md)
        print('%-16s lives %c -> %c, in gameplay again: %d' % (','.join(c) or 'none', before, md.ram8(LIVES), md.info & 1))
    for c in ((), ('inf_apples',)):
        md = level(c)
        a = md.ram16(APPLES)
        for _ in range(3):
            md.hold('A'); md.mrun(4); md.hold(0); md.mrun(40)
        print('%-16s apples %s -> %s' % (','.join(c) or 'none', bytes.fromhex('%04x' % a), bytes.fromhex('%04x' % md.ram16(APPLES))))
    # gems: the spend routine ($1B03BE) on a stock of 05
    for c in ((), ('inf_gems',)):
        md = level(c)
        md.set16(0xFFEFE2, 0x3035)
        md.bp_clear()
        print('%-16s gems routine first word %04x' % (','.join(c) or 'none', md.rom[0x1B03BE] << 8 | md.rom[0x1B03BF]))
    # continues: lives '1' -> die -> game over / continue screen
    for c in ((), ('inf_continues',)):
        md = level(c)
        md.set8(LIVES, 0x30)
        cont = md.ram8(CONT)
        die(md, 300)
        for _ in range(30):
            md.hold('START'); md.mrun(4); md.hold(0); md.mrun(30)
        print('%-16s continues %d -> %d, lives %c, level %d, gameplay %d' % (','.join(c) or 'none', cont, md.ram8(CONT),
              md.ram8(LIVES), md.ram8(0xFF7E26), md.info & 1))
        shot(md).save(OUT + '/cheat_cont_%s.png' % (c[0] if c else 'none'))
