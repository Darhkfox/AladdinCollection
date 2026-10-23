"""Sound RE for Aladdin (MD): the GEMS driver's sequence bank, where the game asks for each sequence, and a
headless render of every sequence through the port's jukebox (module) to check that each one sounds.

    python tools/re/md_sound.py table        # sequences: channel counts
    python tools/re/md_sound.py sites        # request sites (pea #id; jsr $1E58B8) with the code before them
    python tools/re/md_sound.py render [ids] # jukebox playback: peak level / length per sequence
    python tools/re/md_sound.py wav <id> [s] # md_out/snd_<id>.wav

The GEMS 68000 library (from $1E56C0) queues commands in the Z80's FIFO at $1B40 (write index $0036): $FF, command,
arguments. gemsinit ($1E584A, called at $1AA35C) gets the banks: patches $1B9D06, envelopes $1BAF46, sequences
$1BAF6F, samples $1C73CB. The game's request is "stop sequence n" (command $12, $1E58B8) then "start sequence n"
($10, $1E589A); $16 stops all ($1E58F4), $0C / $0D pause / resume all (the pause menu).
"""
import struct
import sys
import wave
from md_util import *

SEQ_BANK = 0x1BAF6F


def sequences():
    n = (romb[SEQ_BANK] | romb[SEQ_BANK + 1] << 8) // 2
    out = []
    for i in range(n):
        o = SEQ_BANK + (romb[SEQ_BANK + 2 * i] | romb[SEQ_BANK + 2 * i + 1] << 8)
        out.append((i, o, romb[o]))
    return out


def sites():
    res = {}
    for i in range(0x1A8000, 0x1B8000, 2):
        if romb[i:i + 2] == b'\x48\x78' and romb[i + 4:i + 10] == bytes.fromhex('4eb9001e58b8'):
            res.setdefault(struct.unpack('>H', romb[i + 2:i + 4])[0], []).append(i)
    return res


def level_music():
    return {lv: r16(0x2C78 + lv * 0x42 + 0x1A) for lv in range(13)}


def render(md, sid, frames=60 * 40):
    md.jukebox_boot()
    md.play(sid)
    pcm, peak, last, first = [], 0, 0, None
    for f in range(frames):
        md.jukebox_frame()
        md.run(1)
        a = md.audio()
        pcm += a
        p = (max(a) - min(a)) if a else 0       # (the output idles at a constant level)
        peak = max(peak, p)
        if p > 200:
            last = f
            first = first if first is not None else f
    render.first = first
    return peak, last, pcm


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'table'
    if cmd == 'table':
        lm = level_music()
        for i, o, ch in sequences():
            lv = [k for k, v in lm.items() if v == i]
            print('%3d $%02X  %06x  channels %2d %s' % (i, i, o, ch, ('level %s' % lv) if lv else ''))
    elif cmd == 'sites':
        for sid, at in sorted(sites().items()):
            print('== %d ($%02X): %s' % (sid, sid, ' '.join('%x' % a for a in at)))
            for a in at[:3]:
                for x in insns('md', a - 36, 10):
                    print('    %06x %s %s' % (x[0], x[2], x[3]))
    elif cmd == 'render':
        md = ModuleMD(ROM, 'md')
        ids = [int(a, 0) for a in sys.argv[2:]] or [i for i, o, ch in sequences() if ch]
        for sid in ids:
            peak, last, _ = render(md, sid, 60 * 30)
            f = render.first
            print('%3d $%02X peak %6d sound %s..%.1f s' % (sid, sid, peak, '-' if f is None else '%.1f' % (f / 60), last / 60))
    elif cmd == 'wav':
        md = ModuleMD(ROM, 'md')
        sid = int(sys.argv[2], 0)
        secs = float(sys.argv[3]) if len(sys.argv) > 3 else 20
        peak, last, pcm = render(md, sid, int(60 * secs))
        w = wave.open(OUT + '/snd_%d.wav' % sid, 'wb')
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(int(md.sample_rate))
        w.writeframes(b''.join(struct.pack('<h', max(-32768, min(32767, v))) for v in pcm))
        w.close()
        print('peak', peak, 'last', last / 60)
