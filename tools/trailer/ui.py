"""Trailer footage of the app's own screens (launcher, menus, music player, title screens and story scenes) and the
music bed's recordings (audio only), recorded like record.py (hidden 1920x1080 autotest, one exe copy each):
  python ui.py <name> [...]        names: see UI below ('all': every one; 'music': the bed's recordings)
In-game shots load a bot clip's start state first (the game started with --game). CLICKS (written next to the
recording as <name>.clicks.json) lists the mouse clicks, so the trailer can draw a pointer where they land.
"""
import os, sys, json
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from tb import CAP, ART, S, settings_ini, launch, run_all  # noqa: E402

MDP = 'Aladdin CD - MD+'
MSU = 'Aladdin (USA) (1993) (Action Platform) (MSU1) (Audio Orchestral) (Super Nes)'
# pause menu rows the cursor stops on (all three games: one player)
PAUSE = ['Resume', 'Slot', 'Save State', 'Load State', 'Level Select', 'Cheats', 'Achievements', 'Sound Test',
         'Music Player', 'Options', 'Credits', 'Reset Game', 'Return to Launcher', 'Quit to Desktop']
# launcher Options rows; Launcher & Menus rows
OPTS = ['Video', 'Colour', 'Audio', 'Launcher & Menus', 'Controls - Mega Drive', 'Controls - SNES', 'Controls - SMS']
LM = ['Menu Style', 'md', 'snes', 'sms', 'Game Covers', 'Custom Background', 'Cover Shape']
CARD_X = {'md': 0.214, 'snes': 0.5, 'sms': 0.786}
CARD_Y = 0.55


def clicks_file(name, clicks):
    json.dump(clicks, open(os.path.join(CAP, name + '.clicks.json'), 'w'))


def to_options(s):
    """launcher (a card chosen): down to the buttons (Music Player), left twice: Options"""
    return s.tap('down', gap=24).tap('left', gap=24).tap('left', gap=24).tap('enter', gap=50)


def in_game(s, state, settle=40, pads=True):
    s.at(60).cmd('loadstate ' + state)
    if pads: s.cmd('pads ' + state.split('__')[0])   # (a pad stream overrides the keyboard while it lasts)
    s.wait(settle)
    return s


def pause_to(s, item, gap=10):
    s.tap('esc', gap=36)
    s.tap('down', PAUSE.index(item), gap=gap).wait(14)
    return s


def ini(game='md', **kw):
    return settings_ini(game, kw.pop('wide', 2), **kw)


# ---------------------------------------------------------------- launcher
def ui_launcher():
    """Arabian Nights: the live previews (they need ~10 s to reach the titles), keys, then the mouse"""
    s = S(30).at(700).cmd('record ui_launcher').wait(150)
    clicks = []
    for k in ('right', 'right'):
        s.tap(k, gap=170)
    for g in ('md', 'snes', 'sms'):
        clicks.append((s.f - 700, CARD_X[g], CARD_Y))
        s.click(CARD_X[g], CARD_Y, gap=130)
    s.wait(60).cmd('stoprec')
    clicks_file('ui_launcher', clicks)
    return launch('ui_launcher', ini('md'), s.text())


def ui_style():
    """Options > Launcher & Menus > Menu Style: Arabian Nights -> Classic, back on the launcher"""
    s = S(30).at(700).cmd('record ui_style').wait(60)
    to_options(s).tap('down', OPTS.index('Launcher & Menus'), gap=20).tap('enter', gap=70)
    s.tap('right', gap=120).tap('esc', gap=40).tap('esc', gap=90)
    s.tap('up', gap=40).tap('right', gap=160).tap('right', gap=160).wait(60).cmd('stoprec')
    return launch('ui_style', ini('md'), s.text())


def ui_covers():
    """our own covers (whole) on our own background; Cover Shape: Fill the Frame"""
    s = S(30).at(120).cmd('record ui_covers').wait(120)
    s.tap('right', gap=100).tap('right', gap=100)
    to_options(s).tap('down', OPTS.index('Launcher & Menus'), gap=20).tap('enter', gap=60)
    s.tap('down', LM.index('Cover Shape'), gap=18).wait(30).tap('right', gap=90).tap('esc', gap=40).tap('esc', gap=100)
    s.tap('up', gap=40).tap('left', gap=100).tap('left', gap=120).cmd('stoprec')
    return launch('ui_covers', ini('sms', launcher={'covers': 1, 'background': 1}), s.text(),
                  covers=os.path.join(ART, 'covers'))


def ui_showgames():
    """Show Games: the Master System switched off, its card goes"""
    s = S(30).at(700).cmd('record ui_showgames').wait(40)
    to_options(s).tap('down', OPTS.index('Launcher & Menus'), gap=20).tap('enter', gap=60)
    s.tap('down', LM.index('sms'), gap=20).wait(30).tap('enter', gap=80).tap('esc', gap=40).tap('esc', gap=100)
    s.tap('up', gap=40).tap('right', gap=120).wait(60).cmd('stoprec')
    return launch('ui_showgames', ini('md'), s.text())


def ui_update_credits():
    """a newer release found (faked in the settings: no network in --autotest): the badge; then Credits"""
    s = S(30).at(400).cmd('record ui_update').wait(330).cmd('stoprec')
    s.wait(10).cmd('record ui_credits').wait(20)
    s.tap('down', gap=24).tap('right', gap=24).tap('enter', gap=60).tap('down', 6, gap=30).wait(60).cmd('stoprec')
    return launch('ui_update', ini('md', extra={'updates': {'check': 1, 'latest': 'v1.1'}}), s.text())


def ui_controls():
    """Options > Controls - Mega Drive: rebind B to V, then Menu Buttons"""
    s = S(30).at(300).cmd('record ui_controls').wait(30)
    to_options(s).tap('down', OPTS.index('Controls - Mega Drive'), gap=20).tap('enter', gap=60)
    s.tap('down', 5, gap=16).wait(20).tap('enter', gap=60).tap('v', gap=70)
    s.tap('down', 16, gap=9).wait(30).tap('right', gap=90).tap('left', gap=60).cmd('stoprec')
    return launch('ui_controls', ini('md'), s.text())


SEED = '\n'.join(['[unlocked]', 'md_market=2026-11-20 19:04:11', 'md_desert=2026-11-20 19:31:52',
                  'md_abu=2026-11-20 19:12:40', 'md_peddler=2026-11-21 20:02:05', 'snes_agrabah=2026-11-22 18:44:30',
                  'snes_wheel=2026-11-22 18:51:02', 'snes_debug=2026-11-22 19:30:17', 'sms_market=2026-11-23 21:15:09',
                  'sms_rooftops=2026-11-23 21:40:44', '', '[progress]', 'md_apples=31', 'snes_hearts=6', '',
                  '[general]', 'notice_shown=1', ''])


def ui_ach():
    """launcher > Achievements: scroll the list, then Export to CSV"""
    s = S(30).at(300).cmd('record ui_ach').wait(30)
    s.tap('down', gap=24).tap('left', gap=24).tap('enter', gap=70)
    s.tap('down', gap=50).tap('enter', gap=200)
    s.tap('pgdn', gap=60).tap('pgdn', gap=60).tap('pgdn', gap=80).cmd('stoprec')
    return launch('ui_ach', ini('md', general={'achievements': 1}), s.text(), files={'achievements.ini': SEED})


def ui_toast():
    """an achievement earned in a game: the pop-up (autotest "award", the real pop-up) over the Desert"""
    s = S()
    in_game(s, 'md_desert__0', 20).cmd('record ui_toast').wait(60).cmd('award md_desert').wait(330).cmd('stoprec')
    return launch('ui_toast', ini('md', wide=4, general={'achievements': 1}), s.text(), game='md',
                  files={'achievements.ini': SEED.replace('md_desert=2026-11-20 19:31:52\n', '')})


# ---------------------------------------------------------------- in a game: level select, cheats, states, options
def level_select(name, game, state, rows, wide=2):
    s = S()
    in_game(s, state).cmd('record ' + name).wait(20)
    pause_to(s, 'Level Select').tap('enter', gap=50)
    s.tap('down', rows, gap=12).wait(40).tap('enter', gap=50).tap('enter', gap=0).wait(420).cmd('stoprec')
    return launch(name, ini(game, wide=wide), s.text(), game=game)


def cheats_md():
    """Invincibility, then the developers' level skip: pause (Start), A - the level ends, Level Complete (21:9)"""
    s = S()
    in_game(s, 'md_desert__0', pads=False).cmd('record ch_md').wait(20)
    pause_to(s, 'Cheats').tap('enter', gap=50)
    s.tap('enter', gap=40).tap('down', 8, gap=12).wait(20).tap('enter', gap=60).tap('esc', gap=24).tap('esc', gap=60)
    s.tap('enter', gap=40).tap('z', gap=480).cmd('stoprec')
    return launch('ch_md', ini('md', wide=4), s.text(), game='md')


def cheats_snes():
    """Invincibility and Capcom's Debug Mode, then Reset Game: the title's stage select (hold L, up / down)"""
    s = S()
    in_game(s, 'snes_market__0', pads=False).cmd('record ch_snes').wait(20)
    pause_to(s, 'Cheats').tap('enter', gap=50)
    s.tap('enter', gap=40).tap('down', 5, gap=12).wait(20).tap('enter', gap=60).tap('esc', gap=24)
    s.tap('down', PAUSE.index('Reset Game') - PAUSE.index('Cheats'), gap=10).tap('enter', gap=30).tap('up', gap=20)
    s.tap('enter', gap=20).wait(10)
    for k in range(14):   # START through the logos to the title
        s.tap('enter', gap=60)
    s.down('q').wait(30).tap('up', gap=40).tap('up', gap=40).tap('right', gap=60).up('q').wait(60).cmd('stoprec')
    return launch('ch_snes', ini('snes'), s.text(), game='snes')


def cheats_sms():
    """Invincibility (the developers' AIQY) and the Developer Menu, then Reset Game: PASSWORD opens TEST MODE"""
    s = S()
    in_game(s, 'sms_market__0', pads=False).cmd('record ch_sms').wait(20)
    pause_to(s, 'Cheats').tap('enter', gap=50)
    s.tap('enter', gap=40).tap('down', 3, gap=12).wait(20).tap('enter', gap=60).tap('esc', gap=24)
    s.tap('down', PAUSE.index('Reset Game') - PAUSE.index('Cheats'), gap=10).tap('enter', gap=30).tap('up', gap=20)
    s.tap('enter', gap=420)
    s.tap('z', gap=150).tap('down', gap=60).tap('z', gap=300).tap('down', gap=50).tap('down', gap=50).wait(90).cmd('stoprec')
    return launch('ch_sms', ini('sms'), s.text(), game='sms')


def states_md():
    """quick save (F5) while playing, quick load (F9): back to that moment"""
    s = S()
    in_game(s, 'md_dungeon__0', 0).cmd('record st_md').wait(90).tap('f5', gap=200).tap('f9', gap=160).cmd('stoprec')
    return launch('st_md', ini('md'), s.text(), game='md')


def video_live():
    """Options > Video in the Rug Ride... (the Desert): Widescreen stepped live 4:3 -> 21:9, Pixel Aspect, Filter"""
    s = S()
    in_game(s, 'md_desert__0', 30).cmd('record opt_video').wait(10)
    pause_to(s, 'Options').tap('enter', gap=50).tap('enter', gap=50)   # Options > Video
    s.tap('down', 3, gap=16).wait(30)                                  # Widescreen
    s.tap('left', 4, gap=50).wait(30).tap('right', 4, gap=70).wait(40)
    s.tap('down', 4, gap=14).wait(20).tap('right', 4, gap=50).tap('left', 3, gap=30).wait(30)
    s.tap('down', gap=30).tap('enter', gap=60).tap('down', 6, gap=16).tap('enter', gap=150)
    s.cmd('stoprec')
    return launch('opt_video', ini('md', wide=4), s.text(), game='md')


def slowdown_opt():
    """SNES: Options > Remove Slowdown switched off and on again (its description shown)"""
    s = S()
    in_game(s, 'snes_river__0', 30, pads=False).cmd('record opt_slow').wait(10)
    pause_to(s, 'Options').tap('enter', gap=50).tap('down', 7, gap=14).wait(60)
    s.tap('enter', gap=70).tap('enter', gap=90).wait(30).cmd('stoprec')
    return launch('opt_slow', ini('snes'), s.text(), game='snes')


# ---------------------------------------------------------------- music player
def music_player(name, game, track, seconds, tabs=(), soundtrack=None, packs=False):
    """launcher > Music Player (the game's card chosen), track `track`, Tab (Original <-> pack) at `tabs` seconds"""
    s = S(30).at(200).tap('down', gap=30).tap('enter', gap=60).tap('down', track, gap=8).wait(30)
    s.cmd('record ' + name).wait(30).tap('enter', gap=0)
    t0 = s.f
    for t in tabs:
        s.at(t0 + int(t * 60)).tap('tab', gap=0)
    s.at(t0 + int(seconds * 60)).cmd('stoprec')
    audio = {'soundtrack_' + game: soundtrack} if soundtrack else None
    return launch(name, ini(game, audio=audio), s.text(), packs=packs)


def bed(name, game, track, seconds, soundtrack=None):
    """the music bed: a Music Player track, sound only"""
    s = S(30).at(200).tap('down', gap=30).tap('enter', gap=60).tap('down', track, gap=8).wait(30)
    s.cmd('recaudio ' + name).wait(2).tap('enter', gap=0).wait(int(seconds * 60)).cmd('stoprec')
    audio = {'soundtrack_' + game: soundtrack} if soundtrack else None
    return launch(name, ini(game, audio=audio), s.text(), packs=bool(soundtrack))


# ---------------------------------------------------------------- title screens and story scenes
def md_title():
    """the Mega Drive game from power-on at 21:9 with the MD+ pack: the Genie round the logos, the wide title"""
    s = S(1).cmd('record md_title').wait(2700).cmd('stoprec')
    return launch('md_title', ini('md', wide=4, audio={'soundtrack_md': MDP}), s.text(), game='md', packs=True)


def sms_intro():
    """the Master System game from power-on at 16:9: the Genie's intro and the title"""
    s = S(1).cmd('record sms_intro').wait(2400).cmd('stoprec')
    return launch('sms_intro', ini('sms', wide=2), s.text(), game='sms')


def sms_story():
    """GAME START: the opening story scenes, wide (characters walk in from the wide picture's edge)"""
    s = S(1).at(500).hold('z', 12).wait(200).cmd('record sms_story').hold('z', 12).wait(30).hold('z', 12).wait(4200)
    s.cmd('stoprec')
    return launch('sms_story', ini('sms', wide=2), s.text(), game='sms')


def snes_msu_play():
    """the SNES game with the MSU-1 pack over gameplay (the bot's Agrabah run)"""
    s = S()
    in_game(s, 'snes_market__0', 0).cmd('record snes_msu_play').wait(470).cmd('stoprec')
    return launch('snes_msu_play', ini('snes', audio={'soundtrack_snes': MSU}), s.text(), game='snes', packs=True)


UI = {
    'ui_launcher': ui_launcher, 'ui_style': ui_style, 'ui_covers': ui_covers, 'ui_showgames': ui_showgames,
    'ui_update': ui_update_credits, 'ui_controls': ui_controls, 'ui_ach': ui_ach, 'ui_toast': ui_toast,
    'ls_md': lambda: level_select('ls_md', 'md', 'md_market__0', 8),
    'ls_snes': lambda: level_select('ls_snes', 'snes', 'snes_market__0', 6),
    'ls_sms': lambda: level_select('ls_sms', 'sms', 'sms_market__0', 7),
    'ch_md': cheats_md, 'ch_snes': cheats_snes, 'ch_sms': cheats_sms, 'st_md': states_md, 'opt_video': video_live, 'opt_slow': slowdown_opt,
    'mp_md': lambda: music_player('mp_md', 'md', 2, 9),
    'mp_mdplus': lambda: music_player('mp_mdplus', 'md', 10, 18, tabs=(7, 12), soundtrack=MDP, packs=True),
    'mp_snes': lambda: music_player('mp_snes', 'snes', 5, 18, tabs=(7, 12), soundtrack=MSU, packs=True),
    'mp_sms': lambda: music_player('mp_sms', 'sms', 6, 9),
    'md_title': md_title, 'sms_intro': sms_intro, 'sms_story': sms_story, 'snes_msu_play': snes_msu_play,
}
MUSIC = {
    'bed_snes_title':  lambda: bed('bed_snes_title', 'snes', 2, 40),
    'bed_md_prince':   lambda: bed('bed_md_prince', 'md', 2, 45),
    'bed_snes_agrabah': lambda: bed('bed_snes_agrabah', 'snes', 5, 45),
    'bed_sms_agrabah': lambda: bed('bed_sms_agrabah', 'sms', 2, 45),
    'bed_mdp_onejump': lambda: bed('bed_mdp_onejump', 'md', 4, 50, MDP),
    'bed_msu_awnw':    lambda: bed('bed_msu_awnw', 'snes', 10, 50, MSU),
    'bed_msu_agrabah': lambda: bed('bed_msu_agrabah', 'snes', 5, 50, MSU),
    'bed_mdp_title':   lambda: bed('bed_mdp_title', 'md', 0, 40, MDP),
    'bed_md_friend':   lambda: bed('bed_md_friend', 'md', 10, 45),
    'bed_sms_awnw':    lambda: bed('bed_sms_awnw', 'sms', 6, 45),
    # the games' own chip music in place of the soundtrack packs (YouTube's Content ID matches the packs' film recordings)
    'bed_md_title':    lambda: bed('bed_md_title', 'md', 0, 40),
    'bed_md_onejump':  lambda: bed('bed_md_onejump', 'md', 4, 50),
    'bed_md_camel':    lambda: bed('bed_md_camel', 'md', 3, 45),
    'bed_snes_awnw':   lambda: bed('bed_snes_awnw', 'snes', 10, 50),
}
UI.update(MUSIC)


if __name__ == '__main__':
    names = sys.argv[1:]
    if names == ['all']: names = list(UI)
    elif names == ['music']: names = list(MUSIC)
    run_all([(n, UI[n]) for n in names], parallel=int(os.environ.get('PAR', 4)))
