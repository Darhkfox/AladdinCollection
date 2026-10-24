"""The attract demos with the Debug Mode cheat on (it sets $0009 every frame, so the title shows its stage select and
the work RAM can't match the original's): per demo frame, the demo's state - its level, Aladdin's position, hearts,
apples, gems, the camera - against the run without cheats.

    python snes_attract_debug.py [ext] [frames]"""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import SNES  # noqa

ext = int(sys.argv[1]) if len(sys.argv) > 1 else 0
frames = int(sys.argv[2]) if len(sys.argv) > 2 else 30000
runs = []
for cheats in ('', 'debug_mode'):
    s = SNES()
    s.module('snes')
    s.configure(ext, True, cheats)
    runs.append(s)


def state(s):
    if s.r8(0x331) != 2:
        return None
    return (s.r8(0x35E), s.r16(0x8E7), s.r16(0x8EA), s.r8(0x367), s.r8(0x369), s.r8(0x36B), s.r16(0x1773), s.r16(0x1777))


demo, diff, first = 0, 0, None
for f in range(frames):
    for s in runs:
        s.run(1)
    a, b = state(runs[0]), state(runs[1])
    if a is not None or b is not None:
        demo += 1
        if a != b:
            diff += 1
            if first is None:
                first = (f, a, b)
print('ext %d: %d frames, %d in a demo, the demo differs with Debug Mode in %d%s'
      % (ext, frames, demo, diff, ' (first %s)' % (first,) if first else ''))
