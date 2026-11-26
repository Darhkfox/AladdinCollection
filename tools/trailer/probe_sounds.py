"""Lists each game's music tracks in Music Player order (index, id, name)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tb  # noqa: F401
from mdt import ModuleMD, ROM
md = ModuleMD(ROM, 'md')
print('MD', [(i, s[2]) for i, s in enumerate([s for s in md.sounds() if s[1]])])
from smst import SMS
m = SMS(); m.module('sms')
print('SMS', [(i, s) for i, s in enumerate(m.sounds())][:24])
