"""Gameplay clips for the trailer (searched by bot.py, recorded by record.py).

name: game ('md' / 'snes' / 'sms'), level (the level select's index), wide (settings.ini widescreen 0-4: 4:3, 16:10,
      16:9, 2:1, 21:9; CRT pixels), n (recorded frames), pre (frames played before recording starts), dir
      ('right' / 'left'), intro (moves before the search), survive (no progress to make: just stay clean),
      cheats, variant (the level select's variant: Mega Drive difficulty, 1 = Normal),
      takes: {suffix: dict(wide=, turbo=, hud=, video=...)}: the same inputs recorded again with other settings;
      takes listed in 'lockstep' are searched together (the plan is clean in every one of them),
      ff (recorded holding fast forward), rewind (frames, loops): after `frames` of play R is held for `loops`.
"""
from tb import ext_of

CLIPS = {
    # ---- the three games, introduced (16:9)
    'md_market':    dict(game='md', level=0, wide=2, n=480, pre=40),
    'snes_market':  dict(game='snes', level=0, wide=2, n=480, pre=40),
    'sms_market':   dict(game='sms', level=0, wide=2, n=480, pre=40),
    # ---- widescreen comparisons: the same run at 4:3, 16:9 and 21:9 (searched in lockstep)
    'md_cmp':       dict(game='md', level=0, wide=2, n=600, pre=540, takes={'w0': dict(wide=0), 'w4': dict(wide=4)},
                         lockstep=('w0', 'w4')),
    'snes_cmp':     dict(game='snes', level=0, wide=2, n=600, pre=520, takes={'w0': dict(wide=0), 'w4': dict(wide=4)},
                         lockstep=('w0', 'w4')),
    'sms_cmp':      dict(game='sms', level=1, wide=2, n=600, pre=40, takes={'w0': dict(wide=0), 'w4': dict(wide=4)},
                         lockstep=('w0', 'w4')),
    # ---- Mega Drive showcase
    'md_rug':       dict(game='md', level=8, wide=4, n=600, pre=60, survive=True),
    'md_jafar':     dict(game='md', level=12, wide=4, n=480, pre=60, survive=True),
    'md_desert':    dict(game='md', level=2, wide=4, n=360, pre=420),
    'md_dungeon':   dict(game='md', level=4, wide=2, n=360, pre=60),
    'md_cave':      dict(game='md', level=5, wide=2, n=360, pre=60),
    'md_escape':    dict(game='md', level=7, wide=2, n=360, pre=60),
    'md_palace':    dict(game='md', level=10, wide=4, n=360, pre=60, dir='left'),
    'md_quarters':  dict(game='md', level=11, wide=2, n=360, pre=60),
    # ---- SNES showcase
    'snes_lamp3':   dict(game='snes', level=10, wide=4, n=420, pre=40, survive=True),
    'snes_cave':    dict(game='snes', level=4, wide=2, n=360, pre=60),
    'snes_river':   dict(game='snes', level=5, wide=2, n=360, pre=60, takes={'orig': dict(turbo=1)}),
    'snes_lava':    dict(game='snes', level=6, wide=2, n=360, pre=60),
    'snes_lamp1':   dict(game='snes', level=8, wide=2, n=360, pre=60),
    'snes_dungeon': dict(game='snes', level=11, wide=4, n=360, pre=60),
    'snes_awnw':    dict(game='snes', level=14, wide=2, n=360, pre=60, survive=True),
    'snes_slow':    dict(game='snes', level=5, wide=2, n=1200, pre=40, takes={'orig': dict(turbo=1)}),
    'snes_slow2':   dict(game='snes', level=6, wide=2, n=1200, pre=40, takes={'orig': dict(turbo=1)}),
    'snes_palace':  dict(game='snes', level=15, wide=2, n=360, pre=60),
    # ---- Master System showcase
    'sms_caverns':  dict(game='sms', level=2, wide=4, n=360, pre=60),
    'sms_treasure': dict(game='sms', level=3, wide=2, n=360, pre=60),
    'sms_escape':   dict(game='sms', level=5, wide=2, n=360, pre=60, survive=True),
    'sms_palace':   dict(game='sms', level=6, wide=4, n=360, pre=60),
    'sms_awnw':     dict(game='sms', level=7, wide=2, n=360, pre=60, survive=True),
    'sms_jafar':    dict(game='sms', level=8, wide=2, n=360, pre=60),
    # ---- features shared by all three
    'sms_ff':       dict(game='sms', level=1, wide=2, n=200, pre=40, ff=True),
    'snes_rew':     dict(game='snes', level=8, wide=2, n=420, pre=60, rewind=(300, 120)),
    'md_hud':       dict(game='md', level=2, wide=4, n=300, pre=420, takes={'hud0': dict(hud=0)}, lockstep=('hud0',)),
    'md_sq':        dict(game='md', level=0, wide=2, n=300, pre=200, takes={'sq': dict(video={'aspect': 1})},
                         lockstep=('sq',)),
    'md_col':       dict(game='md', level=4, wide=2, n=420, pre=60,
                         takes={'vivid': dict(video={'color': 2}), 'warm': dict(video={'color': 3}),
                                'gray': dict(video={'color': 5}), 'lcd': dict(video={'color': 7})}),
    'snes_filt':    dict(game='snes', level=8, wide=2, n=300, pre=200,
                         takes={'nearest': dict(video={'filter': 0}), 'scan': dict(video={'filter': 3}),
                                'crt': dict(video={'filter': 4}),
                                'royale': dict(video={'slang_preset': r'shaders\crt\crt-royale.slangp'}),
                                'lottes': dict(video={'slang_preset': r'shaders\crt\crt-lottes.slangp'}),
                                'geom': dict(video={'slang_preset': r'shaders\crt\crt-geom.slangp'})}),
}


def takes(c):
    """[(suffix, settings)] the clip's recordings: the main one ('') and its takes"""
    out = [('', dict(wide=c['wide']))]
    for k, v in c.get('takes', {}).items():
        out.append((k, dict(dict(wide=c['wide']), **v)))
    return out


def variants(c):
    """the headless configurations the bot searches in lockstep: the main take and the takes named in 'lockstep'"""
    out = []
    for suffix, t in takes(c):
        if suffix and suffix not in c.get('lockstep', ()):
            continue
        out.append(dict(ext=ext_of(c['game'], t['wide'], t.get('video', {}).get('aspect', 0)), cheats=c.get('cheats', ()),
                        hud=bool(t.get('hud', 1)), turbo=t.get('turbo', 4)))
    return out
