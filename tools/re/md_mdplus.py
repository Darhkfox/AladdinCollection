"""An MD+ soundtrack pack with the Mega Drive game (soundtrack.cpp, session_md.cpp): the ROM the app runs with the
pack (its BPS patch on the original, the patch's own boot screen skipped, known mistakes of the pack corrected, as
soundtrack_rom does), the CD commands it sends through the MD+ overlay ($3F7FA / $3F7FE: mdt cart_log), and what the
game does with them at every width.

    python tools/re/md_mdplus.py <pack folder> rom                  the ROM: patch, boot screen, fixes, checksum
    python tools/re/md_mdplus.py <pack folder> boot                 power-on to the title and the first demo
    python tools/re/md_mdplus.py <pack folder> levels [ext ...]     each level's start: what plays, at 4:3 and wide
    python tools/re/md_mdplus.py <pack folder> boss [ext ...]       the Cave of Wonders' idol: its tune held in a wide
                                                                    view, then started by the game's code (the pack's)
    python tools/re/md_mdplus.py <pack folder> pause                pause / resume, a life lost, the level's end
    python tools/re/md_mdplus.py <pack folder> attract [frames]     the attract demos play as with the original ROM
    python tools/re/md_mdplus.py <pack folder> jukebox              the music player's map: tune -> track

Commands are shown as frame:play N / pause / resume (track names from the pack's cue sheet).
"""
import os
import sys
import zlib
import hashlib
from md_util import *
from md_boss_tune import Fifo, PATHS, s16

OUT_ROM = os.path.join(OUT, 'mdplus_app.bin')


def bps_apply(src, p):
    assert p[:4] == b'BPS1'
    pos = [4]
    end = len(p) - 12

    def num():
        data, shift = 0, 1
        while True:
            x = p[pos[0]]
            pos[0] += 1
            data += (x & 0x7F) * shift
            if x & 0x80:
                return data
            shift <<= 7
            data += shift
    ssize, tsize, meta = num(), num(), num()
    pos[0] += meta
    assert ssize == len(src) and zlib.crc32(src) & 0xFFFFFFFF == int.from_bytes(p[end:end + 4], 'little'), 'another ROM'
    out = bytearray(tsize)
    o = srel = trel = 0
    while pos[0] < end:
        d = num()
        n = (d >> 2) + 1
        k = d & 3
        if k == 0:
            out[o:o + n] = src[o:o + n]
        elif k == 1:
            out[o:o + n] = p[pos[0]:pos[0] + n]
            pos[0] += n
        else:
            r = num()
            off = (-1 if r & 1 else 1) * (r >> 1)
            if k == 2:
                srel += off
                out[o:o + n] = src[srel:srel + n]
                srel += n
            else:
                trel += off
                for i in range(n):
                    out[o + i] = out[trel]
                    trel += 1
        o += n
    assert zlib.crc32(out) & 0xFFFFFFFF == int.from_bytes(p[end + 4:end + 8], 'little'), 'damaged'
    return out


# soundtrack.cpp kPackFixes
FIXES = {0x4A7C033C: [(0x205101, 0x12, 0x32), (0x205141, 0x32, 0x12)]}


def app_rom(pack):
    orig = open(ROM, 'rb').read()
    bps = [f for f in os.listdir(pack) if f.lower().endswith('.bps')][0]
    out = bps_apply(orig, open(os.path.join(pack, bps), 'rb').read())
    notes = ['patch %s: %d -> %d bytes, CRC %08X' % (bps, len(orig), len(out), zlib.crc32(out) & 0xFFFFFFFF)]
    changed = False
    reset = int.from_bytes(out[4:8], 'big')
    if len(out) > len(orig) and out[4:8] != orig[4:8] and reset & 0xFFFFFF >= len(orig):
        notes.append('boot screen at $%06X skipped (reset vector back to $%06X)' % (reset, int.from_bytes(orig[4:8], 'big')))
        out[4:8] = orig[4:8]
        changed = True
    for at, was, now in FIXES.get(zlib.crc32(bps_apply(orig, open(os.path.join(pack, bps), 'rb').read())) & 0xFFFFFFFF, []):
        if out[at] == was:
            out[at] = now
            changed = True
            notes.append('fixed $%06X: $%02X -> $%02X' % (at, was, now))
    if changed:
        end = int.from_bytes(out[0x1A4:0x1A8], 'big') + 1
        if end <= 0x200 or end > len(out):
            end = len(out)
        s = sum(int.from_bytes(out[a:a + 2], 'big') for a in range(0x200, end - 1, 2)) & 0xFFFF
        out[0x18E:0x190] = s.to_bytes(2, 'big')
    open(OUT_ROM, 'wb').write(out)
    return notes


def track_names(pack):
    names = {}
    cue = [f for f in os.listdir(pack) if f.lower().endswith('.cue')][0]
    cur = None
    for line in open(os.path.join(pack, cue), encoding='utf-8', errors='replace'):
        line = line.strip()
        if line.startswith('FILE '):
            cur = line.split('"')[1]
        elif line.startswith('TRACK '):
            names[int(line.split()[1])] = os.path.splitext(cur)[0]
    return names


def fmt(log, names):
    out = []
    for f, a, v, size in log:
        if a != 0x3F7FE:
            continue
        c, t = v >> 8, v & 0xFF
        out.append('%d:%s' % (f, 'play %s' % names.get(t, '%d (none)' % t) if c == 0x11 else
                              'loop %s' % names.get(t, '%d (none)' % t) if c == 0x12 else
                              'pause' if c == 0x13 else 'resume' if c == 0x14 else hex(v)))
    return out


def machine(ext=0, cheats=()):
    md = ModuleMD(OUT_ROM, 'md')
    md.configure(ext, True, cheats)
    md.cart_log_on()
    return md


if __name__ == '__main__':
    pack, what = sys.argv[1], sys.argv[2]
    args = sys.argv[3:]
    names = track_names(pack)
    for n in app_rom(pack):
        print(n)
    if what == 'boot':
        md = machine()
        for f in range(3000):
            md.mrun(1)
        print('boot:', fmt(md.cart_log(), names))
    elif what == 'levels':
        for E in [int(a) for a in args] or [0, 128]:
            for e, lv in enumerate(ModuleMD(ROM, 'md').levels()):
                md = machine(E)
                ok = start_play(md, e, 1)
                for f in range(600):
                    md.mrun(1)
                print('E=%3d %-28s %s: %s' % (E, lv[1], 'play' if ok else 'NOT STARTED', ' '.join(fmt(md.cart_log(), names))),
                      flush=True)
    elif what == 'boss':
        for name, path in PATHS.items():
            for E in [int(a) for a in args] or [0, 54, 128]:
                md = machine(E, ('invincible',))
                start_play(md, 5, 1)
                md.mrun(10)
                md.set8(0xFF7273, 0xFF)
                fifo = Fifo(md)
                fifo.new()
                md.cart_log()
                spawned = fm = None
                cd = []
                for b, cond in path:
                    md.hold(b)
                    for i in range(2000):
                        md.mrun(1)
                        cx, cy = s16(md.ram16(0xFF7DF6)), s16(md.ram16(0xFF7DF8))
                        if spawned is None and any(md.ram8(0xFF7E40 + k * 0x42) and md.ram8(0xFF7E40 + k * 0x42 + 0x34) == 0xF5
                                                   for k in range(32)):
                            spawned = (cx, cy)
                        if fm is None and (0x10, 50) in fifo.new():
                            fm = (cx, cy)
                        for c in fmt(md.cart_log(), names):
                            cd.append('%s @%s' % (c.split(':', 1)[1], (cx, cy)))
                        if cond(cx, cy):
                            break
                md.hold(0)
                print('%-34s E=%3d: idol spawned at camera %s; FM boss tune queued at %s; CD: %s' % (
                    name, E, spawned, fm, ', '.join(cd)), flush=True)
    elif what == 'pause':
        md = machine(128)
        start_play(md, 0, 1)
        md.cart_log()
        md.mrun(120)
        md.press('START', 4, 60)
        print('pause:', fmt(md.cart_log(), names), 'paused flag', hex(md.ram8(0xFFF158)))
        md.press('START', 4, 60)
        print('resume:', fmt(md.cart_log(), names), 'paused flag', hex(md.ram8(0xFFF158)))
        # a life lost (the game's own death: $FFF0E6, as md_cheats.py's die), then the last life and the continue screen
        md.set8(0xFFF0E6, 0xFF)
        for f in range(900):
            md.mrun(1)
        print('a life lost:', fmt(md.cart_log(), names))
        md.set8(0xFF7E3C, 0x30)
        md.set8(0xFFF0E6, 0xFF)
        for f in range(600):
            md.mrun(1)
        print('the last life lost (the continue screen):', fmt(md.cart_log(), names))
        md.hold('RIGHT')
        md.mrun(150)
        md.hold(0)
        for f in range(800):
            md.hold('START' if f % 20 < 4 and f < 400 else 0)
            md.mrun(1)
        md.hold(0)
        print('continue (YES):', fmt(md.cart_log(), names))
        md.set8(0xFFF0E9, 0xFF)   # the level is over at once
        for f in range(1500):
            md.hold('START' if f % 30 < 3 else 0)
            md.mrun(1)
        md.hold(0)
        print('level end, next level:', fmt(md.cart_log(), names))
    elif what == 'attract':
        frames = int(args[0]) if args else 40000

        def demos(rom):
            md = ModuleMD(rom, 'md')
            md.configure(0)
            if rom == OUT_ROM:
                md.cart_log_on()
            out, cur, h = [], None, []
            for f in range(frames):
                md.mrun(1)
                ram = bytes(md.ram)
                d = ram[0xF57C]
                if d and cur is None:
                    cur = [f, None, ram[0x7E26], 0]
                elif not d and cur is not None:
                    cur[1] = f
                    out.append(tuple(cur))
                    cur = None
                if cur is not None:
                    cur[3] = int.from_bytes(ram[0xF576:0xF57A], 'big')
                    # (the game's state in a demo, outside the stack and the sound variables)
                    h.append((len(out), hashlib.md5(ram[0x7E00:0x7F00] + ram[0xEFE0:0xF000]).digest()[:6]))
            return out, h
        a, ha = demos(ROM)
        b, hb = demos(OUT_ROM)
        print('original demos:', [(hex(l), f0, f1, hex(p)) for f0, f1, l, p in a])
        print('MD+ demos     :', [(hex(l), f0, f1, hex(p)) for f0, f1, l, p in b])
        print('same demos:', a == b)
        for k in range(len(a)):
            fa = [x[1] for x in ha if x[0] == k]
            fb = [x[1] for x in hb if x[0] == k]
            print('  demo %d (level %s): Aladdin / level state the same in %d of %d frames' % (
                k, hex(a[k][2]), sum(1 for x, y in zip(fa, fb) if x == y), max(len(fa), len(fb))))
    elif what == 'jukebox':
        md = machine()
        md.jukebox_boot()
        for sid, music, name in md.sounds():
            if not music:
                continue
            md.cart_log()
            ok = md.music_request(sid)
            print('%3d %-40s %s' % (sid, name, ' '.join(c.split(':', 1)[1] for c in fmt(md.cart_log(), names)) if ok else 'FAILED'))
    elif what == 'rom':
        print('written to', OUT_ROM)
