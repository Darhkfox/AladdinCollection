"""States near each level's right end, for the level-end checks: python snes_end_states.py [levels] [method] [height]

Aladdin (invincible, infinite lives) is taken right from the level's start, by one of:
  carry : every 90 frames his x ($08E7) moves 200 px on and his y is set to the start's (or `height`); he falls from
          there and the camera follows as it does, streaming the level on the way
  hover : every frame 3 px on, held 100 px below the camera's top (levels with pits and water all along)
  push  : an auto-scroller (the Lava Cavern): the camera itself 8 px a frame on, Aladdin held on the screen; the state
          is saved in the air: check it with snes_motion.py's `hover`
The default per level is the one that gets there. Stops some 120 px (an auto-scroller 400) before the level's last
camera x ($1A04) (an arena holding the camera: where it holds) and writes tools/states/snes_end_<level>.state, a 4:3
state for snes_motion.py, snes_rewind.py and snes_edges.py (and snes_apptest.py). Reached this way: Agrabah Market
and Rooftops, the Market at Dusk (its arena), the Lava Cavern, Sultan's Dungeon 3 (its arena), Jafar's Palace 3; the
Carpet Escape ends at its exit before its camera can reach the level's end."""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa
from snes_wide_play import parse  # noqa

DEFAULT = {2: ('carry', 0xA0), 6: ('push', None), 13: ('hover', None)}
levels = parse(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1] != 'all' else list(range(19))

for lv in levels:
    method, height = DEFAULT.get(lv, ('carry', None))
    if len(sys.argv) > 2:
        method = sys.argv[2]
    if len(sys.argv) > 3:
        height = int(sys.argv[3], 0)
    s = SNES()
    s.module('snes')
    s.configure(0, True, 'invincible,inf_lives')
    s.start_level(lv)
    s.run(30)
    y = height if height is not None else s.r16(0x8EA)
    last = s.r16(0x1A04)
    stop = 400 if method == 'push' else 120
    f, best, still = 0, -1, 0
    while s.r16(0x1773) < last - stop and f < 6000 and still < 2500:
        if method == 'carry':
            if f % 90 == 0:
                s.w16(0x8E7, s.r16(0x8E7) + 200)
                s.w16(0x8EA, y)
            s.hold('RIGHT')
        elif method == 'hover':
            s.w16(0x8E7, s.r16(0x8E7) + 3)
            s.w16(0x8EA, s.r16(0x1777) + 100)
            s.hold(0)
        else:
            c = s.r16(0x1773) + 8
            s.w16(0x1773, c)
            s.w16(0x8E7, c + 128)
            s.w16(0x8EA, s.r16(0x1777) + 100)
            s.hold(0)
        s.run(1)
        f += 1
        cam = s.r16(0x1773)
        still = still + 1 if cam <= best else 0
        best = max(best, cam)
    s.hold(0)
    if method != 'push':
        s.run(120)
    cam = s.r16(0x1773)
    ok = cam >= last - stop - 300 and s.info()['gameplay']
    if ok:
        s.save_file(STATES + '/snes_end_%d.state' % lv)
    print('level %2d (%s): camera %d of %d after %d frames%s' % (lv, method, cam, last, f, '' if ok else ' (not reached)'),
          flush=True)
