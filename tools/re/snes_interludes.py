"""What follows each stage's last part (the exit moved to the player, as in snes_finish.py): screens, songs, pictures.
python snes_interludes.py [level indices]  -> tools/qa/snes/interlude_<n>.png"""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa

for n in [int(a) for a in sys.argv[1:]] or [3, 5, 7, 10, 13, 14, 17]:
    s = SNES()
    s.module('snes')
    s.configure(0, True, 'invincible,inf_lives')
    s.start_level(n)
    s.run(200)
    lvl = s.r8(0x35E)
    off = s.r16(0x80EC1E + lvl * 2)
    s.w16(0x80EC1F + off, s.r16(0x08E7))
    s.w16(0x80EC1F + off + 2, s.r16(0x08EA))
    s.bp(0x81FA23)
    ims, lab = [], []
    for i in range(40):
        s.run(150)
        hs = [h for h in s.bp_hits() if h['pc'] == 0x81FA23]
        s.bp_clear()
        s.bp(0x81FA23)
        ims.append(img(s).resize((128, 112)))
        lab.append('%d %02X %s' % (s.frame, s.r8(0x35E), ' '.join('%02X' % (h['a'] & 0xFF) for h in hs)))
        if i % 4 == 3:
            s.press('B', 3, 0)
    sheet(ims, QA + '/interlude_%d.png' % n, cols=10, labels=lab)
    print(n, ' | '.join(l for l in lab if len(l.split()) > 2))
