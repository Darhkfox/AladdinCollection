"""Which song uploads ($81:FA23, A = upload index) the game makes in which screen ($035E), over: the boot and attract
mode, a game with deaths to the continue screen, the bonus wheel and STAGE CLEAR (via snes_finish's states), every
level, and the ending (tools/states/ending.state): python snes_song_log.py"""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa

seen = {}


def log(s, tag):
    for h in s.bp_hits():
        if h['pc'] == 0x81FA23:
            k = h['a'] & 0xFF
            seen.setdefault(k, set()).add('%s lvl%02X' % (tag, s.r8(0x35E)))
    s.bp_clear()
    s.bp(0x81FA23)


s = SNES()
s.bp(0x81FA23)
s.run(6000)                     # logos, story, title, attract demos
log(s, 'boot')
for i in range(19):             # every level's start
    LV = [(0, 0), (0, 1), (0, 2), (0, 3), (1, 0), (1, 1), (2, 0), (2, 1), (3, 0), (3, 1), (3, 2), (4, 0), (4, 1),
          (4, 2), (6, 0), (5, 0), (5, 1), (5, 2), (7, 0)]
    t = SNES()
    t.load_file(STATES + '/title_idle.state')
    t.run(5)
    t.hold('START')
    t.run_to(0x818157, 3000000)
    t.hold(0)
    t.w8(0x7E035C, LV[i][0])
    t.w8(0x7E035D, LV[i][1])
    t.bp(0x81FA23)
    t.run(300)
    log(t, 'level%d' % i)
    if i == 0:                  # die three times: lost a life, continue screen
        for k in range(3):
            t.w8(0x367, 0)
            t.w8(0x0347, t.r8(0x347) | 2)
            t.run(500)
            log(t, 'deaths')
for name in ('after_0', 'after_3', 'ending', 'password', 'title_idle'):
    p = STATES + '/%s.state' % name
    if not os.path.exists(p):
        continue
    t = SNES()
    t.load_file(p)
    t.bp(0x81FA23)
    t.run(4000)
    log(t, name)
for k in sorted(seen):
    print('%02X  %s' % (k, ', '.join(sorted(seen[k]))))
