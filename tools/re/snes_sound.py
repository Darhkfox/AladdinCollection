"""Capcom's sound driver in SNES Aladdin (notes: docs/re_snes_game.md, Sound).

    python snes_sound.py tables         # the level music table and the upload table
    python snes_sound.py survey         # play every song upload and sound effect from the jukebox, measure loudness

The CPU sends one queued byte a frame from the NMI ($80:B463: the ring $03BF, 32 bytes, write index $03BD, read
$03BB; $F5 / $F6 take a parameter byte in port 3). Sound effects: JSL $81:FA00 with A = the id (queued). Songs:
JSL $81:FA23 with A = the upload index (command $FA, then the data block from the table at $85:8000, 3 bytes an
entry); the song starts when its upload ends. The OPTIONS screen's B.G.M. test plays upload $10 + n (n 0-$22)."""
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa


def tables():
    s = SNES()
    lm = [s.r8(0x80B8EF + i) for i in range(0x40)]
    print('level music ($80:B8EF by level index):', ' '.join('%02X' % v for v in lm))
    for i in range(0x40):
        p = s.r24(0x858000 + i * 3)
        size = s.r16(p) if p >= 0x808000 else 0
        print('%02X %06X' % (i, p))


def survey():
    """Each song from the jukebox: loudness, and how long it sounds before a second of silence (a jingle ends)."""
    import array
    for sid in [int(a, 16) for a in sys.argv[2:]] or list(range(0x10, 0x37)):
        s = SNES()
        s.module('snes')
        s.jukebox_boot()
        s.audio_on()
        s.play(sid)
        a = array.array('h', s.run_audio(60 * 40))
        n = len(a) // 2
        sec = 32040
        loud = [max(abs(v) for v in a[2 * i * sec // 10: 2 * (i + 1) * sec // 10] or [0]) for i in range(n * 10 // sec)]
        end = len(loud)
        quiet = 0
        for i, v in enumerate(loud):
            quiet = quiet + 1 if v < 300 else 0
            if quiet >= 10 and i > 15:
                end = i - 9
                break
        rms = (sum(v * v for v in a) / max(1, len(a))) ** 0.5
        print('%02X peak %5d rms %6.0f  sounds %4.1fs %s' % (sid, max(loud or [0]), rms, end / 10.0,
                                                          'jingle' if end < len(loud) else 'loops'))


if __name__ == '__main__':
    {'tables': tables, 'survey': survey}[sys.argv[1]]()
