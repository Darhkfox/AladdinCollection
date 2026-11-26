"""Review of the finished trailer:
  python check.py stills      a still at the start, middle and end of every scene and at every caption's full fade-in
                              (trailer_work/review/*.png, plus one sheet per 12 scenes)
  python check.py black       ffmpeg blackdetect / freezedetect over the encode
  python check.py loud        integrated loudness and true peak of the whole and of each music section
  python check.py packs       each music section that should be a soundtrack pack's track correlated against the
                              pack's files (the section's audio from the final mp4)
"""
import os, sys, json, subprocess, wave
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from tb import WORK, PACKS  # noqa: E402

MP4 = os.path.join(WORK, 'Aladdin_Collection_PC_trailer.mp4')
REVIEW = os.path.join(WORK, 'review')
MDP = os.path.join(PACKS, 'soundtracks', 'Aladdin CD - MD+')
MSU = os.path.join(PACKS, 'msu', 'Aladdin (USA) (1993) (Action Platform) (MSU1) (Audio Orchestral) (Super Nes)')
PACK_OF = {'bed_mdp_title': 'md', 'bed_mdp_onejump': 'md', 'bed_msu_agrabah': 'snes', 'bed_msu_awnw': 'snes'}


def frame_at(t, w=1920):
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-ss', '%.3f' % t, '-i', MP4, '-frames:v', '1', '-vf', 'scale=%d:-1' % w,
                          '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'], capture_output=True).stdout
    h = len(raw) // (w * 3)
    return Image.fromarray(np.frombuffer(raw, np.uint8).reshape(h, w, 3))


def stills():
    os.makedirs(REVIEW, exist_ok=True)
    marks = json.load(open(os.path.join(WORK, 'scene_times.json')))
    end = float(subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', MP4],
                               capture_output=True, text=True).stdout)
    ims = []
    for k, (name, t0) in enumerate(marks):
        t1 = marks[k + 1][1] if k + 1 < len(marks) else end
        for tag, t in (('a', t0 + 0.05), ('c', t0 + 0.5), ('m', (t0 + t1) / 2), ('z', t1 - 0.08)):
            im = frame_at(t)
            im.save(os.path.join(REVIEW, '%02d_%s_%s.png' % (k, name, tag)))
            ims.append((name + ' ' + tag, im.resize((480, 270))))
    for s in range(0, len(ims), 48):
        part = ims[s:s + 48]
        sheet = Image.new('RGB', (480 * 4, 280 * ((len(part) + 3) // 4)), (20, 20, 20))
        for j, (lab, im) in enumerate(part):
            sheet.paste(im, ((j % 4) * 480, (j // 4) * 280 + 10))
        sheet.save(os.path.join(REVIEW, 'sheet_%d.png' % (s // 48)))
    print('stills in', REVIEW)


def black():
    r = subprocess.run(['ffmpeg', '-hide_banner', '-i', MP4, '-vf', 'blackdetect=d=0.15:pix_th=0.06,freezedetect=n=0.001:d=1.5',
                        '-an', '-f', 'null', '-'], capture_output=True, text=True).stderr
    for line in r.splitlines():
        if 'black_start' in line or 'freeze_start' in line or 'freeze_duration' in line:
            print(line.split(']')[-1].strip())


def ebur(src, ss=None, t=None):
    cmd = ['ffmpeg', '-hide_banner']
    if ss is not None: cmd += ['-ss', '%.2f' % ss, '-t', '%.2f' % t]
    cmd += ['-i', src, '-vn', '-af', 'ebur128=peak=true', '-f', 'null', '-']
    r = subprocess.run(cmd, capture_output=True, text=True).stderr
    s = r[r.rindex('Summary:'):]
    i = float(s.split('I:')[1].split('LUFS')[0])
    tp = float(s.split('Peak:')[1].split('dBFS')[0])
    return i, tp


def loud():
    i, tp = ebur(MP4)
    print('whole: %.1f LUFS integrated, true peak %.1f dBTP' % (i, tp))
    for scene, rec, t0, t1 in json.load(open(os.path.join(WORK, 'music_sections.json'))):
        i, tp = ebur(MP4, t0, t1 - t0)
        print('  %-10s %-18s %6.1f-%6.1f s: %6.1f LUFS, peak %5.1f' % (scene, rec, t0, t1, i, tp))


def mono(x, sr, to=8000):
    x = x.astype(np.float32)
    if x.ndim == 2: x = x.mean(axis=1)
    n = int(len(x) * to / sr)
    return np.interp(np.linspace(0, len(x) - 1, n), np.arange(len(x)), x)


def pack_tracks(game):
    out = []
    if game == 'md':
        for f in sorted(os.listdir(MDP)):
            if f.endswith('.wav'):
                with wave.open(os.path.join(MDP, f)) as w:
                    a = np.frombuffer(w.readframes(w.getnframes()), '<i2').reshape(-1, w.getnchannels())
                    out.append((f, mono(a, w.getframerate())))
    else:
        for f in sorted(os.listdir(MSU)):
            if f.endswith('.pcm'):
                a = np.fromfile(os.path.join(MSU, f), '<i2', offset=8).reshape(-1, 2)
                out.append((f, mono(a, 44100)))
    return out


def best_match(chunk, tracks):
    c = (chunk - chunk.mean()) / (chunk.std() + 1e-9)
    res = []
    for name, tr in tracks:
        if len(tr) < len(c): continue
        n = 1 << int(np.ceil(np.log2(len(tr) + len(c))))
        corr = np.fft.irfft(np.fft.rfft(tr, n) * np.conj(np.fft.rfft(c, n)), n)[:len(tr) - len(c)]
        # normalise by the track's local energy
        cs = np.concatenate([[0], np.cumsum(tr ** 2)])
        e = np.sqrt(cs[len(c):len(tr)] - cs[:len(tr) - len(c)]) + 1e-9
        k = int(np.argmax(corr / e))
        res.append((float(corr[k] / e[k]) / np.sqrt(len(c)), name, k / 8000.0))
    res.sort(reverse=True)
    return res[:2]


def section_audio(t0, t1):
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-ss', '%.2f' % t0, '-t', '%.2f' % (t1 - t0), '-i', MP4, '-vn', '-ac', '1',
                          '-ar', '8000', '-f', 's16le', '-'], capture_output=True).stdout
    return np.frombuffer(raw, '<i2').astype(np.float32)


def packs():
    secs = json.load(open(os.path.join(WORK, 'music_sections.json')))
    tracks = {g: pack_tracks(g) for g in ('md', 'snes')}
    for scene, rec, t0, t1 in secs:
        if rec not in PACK_OF: continue
        a = section_audio(t0 + 1.5, t1 - 1.5)
        for j in range(0, max(1, len(a) // 8000 - 5), 6):
            chunk = a[j * 8000:(j + 5) * 8000]
            if len(chunk) < 5 * 8000: break
            m = best_match(chunk, tracks[PACK_OF[rec]])
            print('%-10s %-16s t=%5.1f s: best %s (%.2f at %.1f s), next %s (%.2f)' % (
                scene, rec, t0 + 1.5 + j, m[0][1], m[0][0], m[0][2], m[1][1], m[1][0]), flush=True)
    # the soundtrack scenes' own sound: the Music Player's pack parts and the MSU-1 gameplay
    marks = dict(json.load(open(os.path.join(WORK, 'scene_times.json'))))
    for scene, game, offs in (('mdplus', 'md', (0.3, 5.5)), ('msu', 'snes', (0.3, 4.6))):
        for o in offs:
            t0 = marks[scene] + o
            chunk = section_audio(t0, t0 + 1.8)
            m = best_match(chunk, tracks[game])
            print('%-10s scene t=%5.1f s: best %s (%.2f at %.1f s), next %s (%.2f)' % (
                scene, t0, m[0][1], m[0][0], m[0][2], m[1][1], m[1][0]), flush=True)


if __name__ == '__main__':
    {'stills': stills, 'black': black, 'loud': loud, 'packs': packs}[sys.argv[1]]()
