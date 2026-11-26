"""Frames of a recording that repeat the one before (the game didn't finish a frame: slowdown):
  python dups.py <rec> [...]     writes <rec>.dups.json (indices) and prints the count"""
import os, sys, json, subprocess
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tb import CAP  # noqa: E402


def dups(name, w=240, h=135):
    p = subprocess.Popen(['ffmpeg', '-v', 'error', '-i', os.path.join(CAP, name + '.mkv'), '-vf', 'scale=%d:%d' % (w, h),
                          '-f', 'rawvideo', '-pix_fmt', 'gray', '-'], stdout=subprocess.PIPE)
    out, last, k = [], None, 0
    while True:
        b = p.stdout.read(w * h)
        if len(b) < w * h: break
        a = np.frombuffer(b, np.uint8).astype(np.int16)
        if last is not None and np.abs(a - last).max() <= 2: out.append(k)
        last = a; k += 1
    json.dump(out, open(os.path.join(CAP, name + '.dups.json'), 'w'))
    return out, k


if __name__ == '__main__':
    for n in sys.argv[1:]:
        d, k = dups(n)
        print(n, len(d), 'of', k, d[:40])
