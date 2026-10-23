"""Sound survey through the module's music player: every driver command $81-$D0 played on its own.

    python sms_sound.py [--wav]       -> length (to silence, or 'loops' if still playing after 4 minutes), loudness,
                                         channels used; --wav writes sms_out/snd_XX.wav (first 20 s)
"""
import sys
import struct
import sms_re as R

MAX_FRAMES = 60 * 240


def survey(sid, wav=False):
    s = R.boot()
    s.module('sms')
    s.jukebox_boot()
    s.jukebox_frame()
    s.play(sid)
    last_sound = 0
    peak = 0
    pcm = []
    for f in range(MAX_FRAMES if sid < 0x95 else 60 * 30):
        s.jukebox_frame()
        s.run(1)
        a = s.audio()
        if wav and f < 60 * 20:
            pcm += a[::2]
        m = max((abs(x) for x in a), default=0)
        if m > 200:
            last_sound = f
            peak = max(peak, m)
        if f - last_sound > 240 and f > 300:
            break
    if wav:
        with open(R.OUT + '/snd_%02X.wav' % sid, 'wb') as fo:
            n = len(pcm)
            rate = 55930
            fo.write(b'RIFF' + struct.pack('<I', 36 + n * 2) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, 1, rate, rate * 2, 2, 16))
            fo.write(b'data' + struct.pack('<I', n * 2))
            fo.write(struct.pack('<%dh' % n, *[max(-32768, min(32767, int(x * 2))) for x in pcm]))
    loops = last_sound >= (MAX_FRAMES if sid < 0x95 else 60 * 30) - 2
    return last_sound / 60.0, loops, peak


if __name__ == '__main__':
    wav = '--wav' in sys.argv
    ids = [int(x, 16) for x in sys.argv[1:] if not x.startswith('--')] or range(0x81, 0xD1)
    for sid in ids:
        t, loops, peak = survey(sid, wav)
        print('%02X %s %6.1fs %s peak %d' % (sid, 'music' if sid < 0x95 else 'sfx  ', t, 'loops' if loops else '', peak))
