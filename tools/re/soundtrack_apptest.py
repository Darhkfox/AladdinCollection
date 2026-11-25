"""The soundtrack packs in the real app (build/bin/AladdinCollection.exe --autotest): a game started from power-on with
its pack chosen, played a little (with sound effects), paused, saved and loaded, rewound, and (SNES) the pack switched
off and on in a level, the sound recorded (autotest `recaudio`, 48 kHz) and checked against the pack's own tracks.

    python tools/re/soundtrack_apptest.py <outdir> md   [--pack "<folder name in build/bin/soundtracks>"]
    python tools/re/soundtrack_apptest.py <outdir> snes [--pack "<folder name in build/bin/msu>"]
    python tools/re/soundtrack_apptest.py <outdir> md|snes --analyse      (again, on the last run's recording)
    python tools/re/soundtrack_apptest.py <outdir> md|snes --jukebox      the Music Player instead (from the launcher):
                                                                          a tune in the pack's version, the original
                                                                          (Tab), the pack's again
    python tools/re/soundtrack_apptest.py <outdir> snes --switch-title   the MSU-1 pack switched off and on on the
                                                                          title, then the game started

The pack's folder must be in build/bin/soundtracks (MD+) or build/bin/msu (MSU-1). For the run, build/bin/settings.ini
is written with the pack chosen, 21:9 square pixels and achievements off (an existing one is put back afterwards).

Analysis: the recording is matched, second by second, against every track of the pack (a cross-correlation of the
mono signal at 8 kHz): which track plays where in it and how closely (1.0 = the track alone), so the music is shown to
be the pack's, its place in the track (it goes back with a loaded state and with rewind, stands still in a pause), and
the residue - the recording less the matched track - shows the console's own sounds (effects) over it.
"""
import os
import sys
import subprocess
import wave
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
BIN = os.path.join(ROOT, 'build', 'bin')
EXE = os.path.join(BIN, 'AladdinCollection.exe')
RATE = 48000
FPS = 60.0   # (the autotest runs a game frame per loop; the recording pads to 48000 / 60 a frame)


def script_md():
    """MD: START through the logos, the title and the story to Agrabah Market; play: sword swings (B: X key), apples
    (A: Z key); pause and resume; F5 / F9; rewind (R held)"""
    s, f = [], 20
    s.append('%d recaudio sound' % f)
    for k in range(14):                       # (to the level: START on the logos, the title, the story card)
        f += 75
        s.append('%d tap enter' % f)
    f += 400
    s.append('%d mark level' % f)
    s.append('%d shot md_level' % f)
    for k in range(6):                        # sword swings and apples: effects over the music
        s.append('%d tap x' % (f + 60 * k))
        s.append('%d tap z' % (f + 60 * k + 30))
    f += 420
    s.append('%d mark pause' % f)
    s.append('%d tap enter' % f)              # the game's own pause (CD paused)
    f += 180
    s.append('%d tap enter' % f)              # resumed
    s.append('%d mark resume' % f)
    f += 180
    s.append('%d tap f5' % f)                 # save state
    s.append('%d mark saved' % f)
    f += 300
    s.append('%d tap f9' % f)                 # load it: the track goes back 5 s
    s.append('%d mark loaded' % f)
    f += 240
    s.append('%d down r' % f)                 # rewind 2 s
    s.append('%d mark rewind' % f)
    f += 120
    s.append('%d up r' % f)
    s.append('%d mark rewound' % f)
    f += 300
    s.append('%d stoprec' % f)
    s.append('%d quit' % (f + 5))
    return '\n'.join(s) + '\n'


def script_snes():
    """SNES: START through the logos and the story, GAME START, Agrabah Market; play: jumps (B: Z key), apples (Y: A
    key); pause and resume (START), F5 / F9, rewind; the pack switched off and on again in the level"""
    s, f = [], 20
    s.append('%d recaudio sound' % f)
    for k in range(16):
        f += 75
        s.append('%d tap enter' % f)
    f += 500
    s.append('%d mark level' % f)
    s.append('%d shot snes_level' % f)
    for k in range(6):
        s.append('%d tap z' % (f + 60 * k))
        s.append('%d tap a' % (f + 60 * k + 30))
    f += 420
    s.append('%d mark pause' % f)
    s.append('%d tap enter' % f)
    f += 180
    s.append('%d tap enter' % f)
    s.append('%d mark resume' % f)
    f += 180
    s.append('%d tap f5' % f)
    s.append('%d mark saved' % f)
    f += 300
    s.append('%d tap f9' % f)
    s.append('%d mark loaded' % f)
    f += 240
    s.append('%d down r' % f)
    s.append('%d mark rewind' % f)
    f += 120
    s.append('%d up r' % f)
    s.append('%d mark rewound' % f)
    f += 300
    s.append('%d set soundtrack=0' % f)       # the pack off in the level: the original music from the song's start
    s.append('%d mark off' % f)
    f += 420
    s.append('%d shot snes_off' % f)
    s.append('%d set soundtrack=1' % f)       # and on again
    s.append('%d mark on' % f)
    f += 420
    s.append('%d stoprec' % f)
    s.append('%d quit' % (f + 5))
    return '\n'.join(s) + '\n'


def script_switch_title():
    """SNES: the pack switched off and on again on the title, then the game started: its songs from then on as from a
    power-on with the pack (no sound from the console's music under the pack's)"""
    s, f = [], 20
    s.append('%d recaudio sound' % f)
    for k in range(5):
        f += 75
        s.append('%d tap enter' % f)
    f += 200
    s.append('%d set soundtrack=0' % f)
    s.append('%d mark off' % f)
    f += 300
    s.append('%d set soundtrack=1' % f)
    s.append('%d mark on' % f)
    for k in range(11):
        f += 75
        s.append('%d tap enter' % f)
    f += 500
    s.append('%d mark level' % f)
    f += 900
    s.append('%d stoprec' % f)
    s.append('%d quit' % (f + 5))
    return '\n'.join(s) + '\n'


def script_jukebox(game):
    s, f = [], 30
    s.append('%d tap down' % f)               # (the launcher: Music Player, for the last game)
    s.append('%d tap enter' % (f + 20))
    f += 400
    s.append('%d recaudio sound' % f)
    s.append('%d mark start' % f)
    s.append('%d tap down' % (f + 10))
    s.append('%d tap down' % (f + 20))
    s.append('%d tap enter' % (f + 40))       # the third tune, in the pack's version
    s.append('%d shot jukebox_pack' % (f + 300))
    f += 600
    s.append('%d tap tab' % f)                # the original
    s.append('%d mark original' % f)
    s.append('%d shot jukebox_original' % (f + 300))
    f += 600
    s.append('%d tap tab' % f)                # the pack again
    s.append('%d mark pack' % f)
    f += 600
    s.append('%d stoprec' % f)
    s.append('%d quit' % (f + 5))
    return '\n'.join(s) + '\n'


def tracks_of(game, pack):
    """{name: mono float32 at 8 kHz} of the pack's tracks"""
    out = {}
    if game == 'md':
        for n in sorted(os.listdir(pack)):
            if n.lower().endswith('.wav'):
                w = wave.open(os.path.join(pack, n))
                a = np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, w.getnchannels()).mean(1)
                out[os.path.splitext(n)[0]] = resample(a, w.getframerate(), 8000)
    else:
        for n in sorted(os.listdir(pack)):
            if n.lower().endswith('.pcm'):
                a = np.fromfile(os.path.join(pack, n), np.int16)[4:].reshape(-1, 2).mean(1)
                out['track ' + n.rsplit('-', 1)[1][:-4]] = resample(a, 44100, 8000)
    return out


def resample(a, src, dst):
    n = int(len(a) * dst / src)
    return np.interp(np.arange(n) * (src / dst), np.arange(len(a)), a).astype(np.float32)


def analyse(outdir, game, pack):
    pcm = np.fromfile(os.path.join(outdir, 'sound.pcm'), np.int16).reshape(-1, 2)
    mono48 = pcm.mean(1)
    rec = resample(mono48, RATE, 8000)
    marks = {}
    for line in open(os.path.join(outdir, 'marks.txt')):
        p = line.split()
        marks[p[0]] = int(p[3])
    start = marks.get('start', 20)   # (recaudio's loop frame)
    tracks = tracks_of(game, pack)
    W = 8000   # one-second windows
    from numpy.fft import rfft, irfft
    print('recording: %.1f s, RMS %.0f' % (len(mono48) / RATE, np.sqrt((mono48 ** 2).mean())))
    print('marks (s into the recording):', ', '.join('%s %.1f' % (k, (v - start) / FPS) for k, v in marks.items()))
    rows = []
    for t0 in range(0, len(rec) - W, W):
        seg = rec[t0:t0 + W]
        e = float(np.sqrt((seg ** 2).mean()))
        best = (0.0, None, 0)
        if e > 30:
            for name, tr in tracks.items():
                if len(tr) < W:
                    continue
                n = 1
                while n < len(tr) + W:
                    n <<= 1
                c = irfft(rfft(tr, n) * np.conj(rfft(seg, n)), n)[:len(tr) - W + 1]
                # normalised by the track's energy under the window (a running sum) and the window's
                cs = np.concatenate([[0], np.cumsum(tr.astype(np.float64) ** 2)])
                en = np.sqrt(np.maximum(cs[W:] - cs[:-W], 1e-9)) * np.sqrt((seg.astype(np.float64) ** 2).sum())
                k = int(np.argmax(c / en))
                v = float(c[k] / en[k])
                if v > best[0]:
                    best = (v, name, k)
        # residue: the window less the best match, scaled
        res = 0.0
        if best[1]:
            m = tracks[best[1]][best[2]:best[2] + W]
            g = float((seg * m).sum() / max((m * m).sum(), 1e-9))
            res = float(np.sqrt(((seg - g * m) ** 2).mean()))
        rows.append((t0 // W, e, best, res))
    for sec, e, (v, name, k), res in rows:
        print('%4d s  RMS %6.0f  %-24s at %6.1f s  match %.2f  residue %6.0f' % (
            sec, e, name or '-', k / 8000.0, v, res))
    return rows


if __name__ == '__main__':
    outdir = os.path.abspath(sys.argv[1])
    game = sys.argv[2]
    pack_name = sys.argv[sys.argv.index('--pack') + 1] if '--pack' in sys.argv else None
    folder = os.path.join(BIN, 'soundtracks' if game == 'md' else 'msu')
    if not pack_name:
        pack_name = [n for n in os.listdir(folder) if os.path.isdir(os.path.join(folder, n))][0]
    pack = os.path.join(folder, pack_name)
    os.makedirs(outdir, exist_ok=True)
    if '--analyse' not in sys.argv:
        for n in ('marks.txt', 'sound.pcm'):
            if os.path.exists(os.path.join(outdir, n)):
                os.remove(os.path.join(outdir, n))
        sp = os.path.join(outdir, 'script.txt')
        open(sp, 'w').write(script_jukebox(game) if '--jukebox' in sys.argv else
                            script_switch_title() if '--switch-title' in sys.argv else
                            script_md() if game == 'md' else script_snes())
        ini = os.path.join(BIN, 'settings.ini')
        old = open(ini, 'rb').read() if os.path.exists(ini) else None
        open(ini, 'w', encoding='utf-8').write(
            '[video]\nwidescreen=4\naspect=1\n[audio]\nvolume=80\nsoundtrack_volume=70\nsoundtrack_%s=%s\n'
            '[general]\nachievements=0\nrewind=1\nlast_game=%s\n' % (game, pack_name, game))
        argv = [EXE] + ([] if '--jukebox' in sys.argv else ['--game', game]) + ['--autotest', outdir, sp, '--capture-size', '1680x720']
        print('argv:', argv)
        try:
            subprocess.run(argv, cwd=ROOT, timeout=1800)
        finally:
            if old is None:
                os.remove(ini)
            else:
                open(ini, 'wb').write(old)
    analyse(outdir, game, pack)
