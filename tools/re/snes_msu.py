"""An MSU-1 soundtrack pack with the SNES game (core/msu1.cpp, soundtrack.cpp, session_snes.cpp): the ROM the app runs
with the pack (its .bps / .ips patch, or its pre-patched .sfc checked against the original as msu_rom_check does), the
chip's tracks as the game asks for them, and what the game does with them.

    python tools/re/snes_msu.py <pack folder> rom                   the pack's ROM against the original: what it changes
    python tools/re/snes_msu.py <pack folder> attract [frames] [conf ...]   from power-on, no input: the same screens and
                                                                    demos as the original ROM at 4:3 (the demos replay
                                                                    recorded input), at each conf (<ext>[t<turbo>])
    python tools/re/snes_msu.py <pack folder> levels [conf ...]     each level's start: the tracks (conf: <ext>[t<turbo>])
    python tools/re/snes_msu.py <pack folder> jukebox               the music player's requests: song -> track
    python tools/re/snes_msu.py <pack folder> audio <wav> [level]   a level's first 20 s with the pack's music mixed in as
                                                                    the app does (32040 Hz WAV), and the loudness of the
                                                                    SPC's own sound in it
    python tools/re/snes_msu.py <pack folder> states                save states / rewind: the chip's state goes back with
                                                                    the game's, and the track goes on from there

Track events are frame:track/flags (p playing, r repeating, m missing).
"""
import os
import sys
import wave
import array
import math
import hashlib
import zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import SNES, ROM  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'snes_out')
os.makedirs(OUT, exist_ok=True)
OUT_ROM = os.path.join(OUT, 'msu_app.sfc')


def pack_files(pack):
    names = os.listdir(pack)
    msu = [n for n in names if n.lower().endswith('.msu')]
    pcm = [n for n in names if n.lower().endswith('.pcm')]
    base = msu[0][:-4] if msu else max({n.rsplit('-', 1)[0] for n in pcm}, key=lambda b: sum(n.startswith(b + '-') for n in pcm))
    patch = [n for n in names if n.lower().endswith(('.bps', '.ips'))]
    rom = [n for n in names if n.lower().endswith(('.sfc', '.smc'))]
    return os.path.join(pack, base), patch, rom


def check(orig, new):
    """msu_rom_check: the same size; differences only in the header's checksum, in the original's free space (runs of
    32 or more of one byte) and in up to 16 hooks of up to 8 bytes"""
    if len(orig) != len(new):
        return False, 'another size'
    free = bytearray(len(orig))
    i = 0
    while i < len(orig):
        j = i
        while j < len(orig) and orig[j] == orig[i]:
            j += 1
        if j - i >= 32:
            free[i:j] = b'\1' * (j - i)
        i = j
    hdr = lambda k: 0x7FDC <= k < 0x7FE0 or 0xFFDC <= k < 0xFFE0
    hooks, freeb, i = [], 0, 0
    while i < len(orig):
        if orig[i] == new[i] or hdr(i):
            i += 1
            continue
        if free[i]:
            freeb += 1
            i += 1
            continue
        last = j = i
        while j < len(orig) and j <= last + 2:
            if orig[j] != new[j] and not free[j] and not hdr(j):
                last = j
            j += 1
        hooks.append((i, last + 1))
        i = last + 1
    ok = len(hooks) <= 16 and all(b - a <= 8 for a, b in hooks)
    return ok, 'hooks %s, %d bytes of new code in free space' % (
        ', '.join('$%06X-$%06X' % (a, b - 1) for a, b in hooks), freeb)


def app_rom(pack):
    base, patch, rom = pack_files(pack)
    orig = open(ROM, 'rb').read()
    if patch:
        raise SystemExit('a patch file: apply it with the app (soundtrack_rom); this script checks pre-patched ROMs')
    new = open(os.path.join(pack, rom[0]), 'rb').read()
    if len(new) % 1024 == 512:
        new = new[512:]
    ok, what = check(orig, new)
    open(OUT_ROM, 'wb').write(new)
    return base, ok, '%s: CRC %08X, %s; %s' % (rom[0], zlib.crc32(new) & 0xFFFFFFFF, what, 'accepted' if ok else 'REFUSED')


def flags(st):
    p = st.split(',')
    f = int(p[2])
    return '%s/%s%s%s' % (p[0][1:], 'p' if f & 2 else '', 'r' if f & 4 else '', 'm' if f & 1 else '')


def machine(rom, base=None, conf='0'):
    ext, _, turbo = conf.partition('t')
    s = SNES(rom)
    s.module('snes')
    s.configure(int(ext), True, '')
    s.turbo(int(turbo or 1))
    if base:
        s.msu_open(base)
    return s


def events(s, frames, every=None):
    out, last = [], None
    for f in range(frames):
        s.run(1)
        if every:
            every(f)
        st = s.msu_state()
        k = flags(st)
        if k != last:
            out.append('%d:%s' % (f, k))
            last = k
    return out


if __name__ == '__main__':
    pack, what = sys.argv[1], sys.argv[2]
    args = sys.argv[3:]
    base, ok, note = app_rom(pack)
    print(note)
    if what == 'attract':
        frames = int(args[0]) if args else 30000
        confs = args[1:] or ['0']

        def run(rom, b, conf='0'):
            s = machine(rom, b, conf)
            screens, last, demo = [], None, []
            for f in range(frames):
                s.run(1)
                ram = s.wram_bytes()
                sc = (ram[0x35E], ram[0x331] == 2)
                if sc != last:
                    screens.append('%02X%s@%d' % (sc[0], 'd' if sc[1] else '', f))
                    last = sc
                if sc[1]:   # (a demo: Aladdin's place and the demo's input position)
                    demo.append(hashlib.md5(bytes(ram[0x0E00:0x0E40]) + bytes(ram[0x0330:0x0340])).digest()[:6])
            return screens, demo
        a = run(ROM, None)
        print('original 4:3:', ' '.join(a[0]))
        for conf in confs:
            b = run(OUT_ROM, base, conf)
            print('MSU-1 %-6s: %s' % (conf, ' '.join(b[0])))
            print('  same screens at the same frames: %s; demo frames with the same state: %d of %d' % (
                a[0] == b[0], sum(1 for x, y in zip(a[1], b[1]) if x == y), max(len(a[1]), len(b[1]))), flush=True)
    elif what == 'levels':
        for conf in args or ['0', '140t4']:
            for e in range(19):
                s = machine(OUT_ROM, base, conf)
                ok = s.start_level(e)
                print('%-6s level %2d %s: %s' % (conf, e, 'play' if ok else 'NOT STARTED', ' '.join(events(s, 900))),
                      flush=True)
    elif what == 'jukebox':
        s = machine(OUT_ROM, base)
        s.jukebox_boot()
        for sid in range(0x10, 0x37):
            s.play(sid)
            for f in range(60):
                s.jukebox_frame()
                s.run(1)
            print('song $%02X: %s' % (sid, flags(s.msu_state())))
    elif what == 'audio':
        out = args[0]
        level = int(args[1]) if len(args) > 1 else 0
        s = machine(OUT_ROM, base)
        s.audio_on(True)
        s.start_level(level)
        smp = s.run_audio(20 * 60)
        a = array.array('h', smp)
        w = wave.open(out, 'wb')
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(32040)
        w.writeframes(a.tobytes())
        w.close()
        s2 = machine(OUT_ROM, None)   # (the same ROM without the chip: only the console's sound)
        s2.audio_on(True)
        s2.start_level(level)
        b = array.array('h', s2.run_audio(20 * 60))
        rms = lambda x: math.sqrt(sum(v * v for v in x) / max(1, len(x)))
        print('written %s: %.1f s; RMS with the pack %.0f, the console alone (effects, no music) %.0f; track %s' % (
            out, len(a) / 2 / 32040, rms(a), rms(b), flags(s.msu_state())))
    elif what == 'states':
        s = machine(OUT_ROM, base)
        s.start_level(0)
        s.run(300)
        st, ms = s.save(), s.msu_state()
        s.run(600)
        later = s.msu_state()
        s.load(st)
        s.msu_set_state(ms)
        back = s.msu_state()
        s.run(600)
        again = s.msu_state()
        print('saved %s; 600 frames on %s; loaded %s; 600 frames on %s (same as before: %s)' % (
            ms, later, back, again, later == again))
