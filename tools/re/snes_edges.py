"""The view at the levels' ends: python snes_edges.py <ext> [levels] [frames]

Each level from its start (a walk right, then left back to the start) and, where tools/states/snes_end_<level>.state
exists (snes_end_states.py), from near its right end (a walk right to the end and back; the Lava Cavern's held in the
air as its camera scrolls on). Per frame the picture's world range [camera - view_left, + 256 + 2 * ext) is checked
against the level's, [0, $1A04 + 256): pixels past it are the mirrored, dimmed margins this is about (an arena
holding the camera, Market at Dusk's boss zone, is dimmed past its limits on purpose: not counted). A frame in a shaft
(every page row of the game's screen holds only the camera's page column: Inside the Lamp 3's tower) is checked to be
centred on that column instead and counted apart ("shaft"; off-centre ones listed). Listed: the most
such pixels on a side in any frame; pictures of the start, the first frame at the right end and the end of each run
to tools/qa/snes/edges_<ext>_<level>.png."""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa

ext = int(sys.argv[1]) if len(sys.argv) > 1 else 140
levels = [int(a) for a in sys.argv[2].split(',')] if len(sys.argv) > 2 and sys.argv[2] != 'all' else list(range(19))
frames = int(sys.argv[3]) if len(sys.argv) > 3 else 600
HOVER = {6}


def page(s, px, py):
    off = s.r16(0x1738 + 0x42)
    return s.r16(0x7FF000 + ((off + (py + 1) * 64 + ((px + 1) * 2 & 0xFE)) & 0xFFF))


def shaft(s, cam, last):
    """The module's span rule (AladdinWide::shaft): the screen's page rows hold only the camera's page column."""
    cy = s.r16(0x1777)
    if cam & 0xFF or cy & 0x8000:
        return False
    c = cam >> 8
    for r in range(cy >> 8, ((cy + 223) >> 8) + 1):
        if page(s, c, r) & 0x8000:
            return False
        if c >= 1 and not page(s, c - 1, r) & 0x8000:
            return False
        if (c + 1) * 256 < last + 256 and not page(s, c + 1, r) & 0x8000:
            return False
    return True


def run(lv, state):
    s = SNES()
    s.module('snes')
    s.configure(ext, True, 'invincible,inf_lives')
    if state:
        s.load_file(state)
    else:
        s.start_level(lv)
    worst, where, shots, labels = [0, 0], ['', ''], [], []
    at_end = False
    sh = [0, 0]   # shaft frames, off-centre ones
    for f in range(frames):
        if state and lv in HOVER:
            s.w16(0x8E7, s.r16(0x1773) + 128)
            s.w16(0x8EA, s.r16(0x1777) + 100)
            s.hold(0)
        else:
            s.hold('RIGHT' if f < frames // 2 else 'LEFT')
        s.run(1)
        el = s.view_left()
        if el is None or s.ppu_regs()['bright'] == 0:   # (not a level, or a black screen: a fade's end)
            continue
        lr = s.line_regs()
        cam = s.r16(0x19ED)   # the camera on screen: BG1's scroll, completed from the NMI's copy
        cam = cam + ((((lr[0][0] - cam) & 0x3FF) + 0x200) & 0x3FF) - 0x200
        last = s.r16(0x1A04)
        lo = cam - el
        hi = lo + 256 + 2 * ext
        if shaft(s, cam, last):
            sh[0] += 1
            sh[1] += el != ext
            continue
        if -lo > worst[0]:
            worst[0], where[0] = -lo, 'f%d cam %d max %d' % (f, cam, last)
        if hi - (last + 256) > worst[1]:
            worst[1], where[1] = hi - (last + 256), 'f%d cam %d max %d' % (f, cam, last)
        end_now = state and hi >= last + 256
        if f == 2 or f == frames - 1 or (end_now and not at_end):
            shots.append(img(s))
            labels.append('L%d %s f%d cam %d view %d..%d (level 0..%d)' % (lv, 'end' if state else 'start', f, cam, lo,
                                                                         hi, last + 256))
        at_end = at_end or end_now
    return worst, where, shots, labels, at_end, sh


for lv in levels:
    w, wh, shots, labels, _, sh = run(lv, None)
    line = 'level %2d start: past the level left %d px%s, right %d px%s' % (
        lv, w[0], ' (%s)' % wh[0] if w[0] else '', w[1], ' (%s)' % wh[1] if w[1] else '')
    if sh[0]:
        line += ' (shaft: %d frames, %d off-centre)' % tuple(sh)
    st = STATES + '/snes_end_%d.state' % lv
    if os.path.exists(st):
        w2, wh2, s2, l2, reached, sh2 = run(lv, st)
        shots += s2
        labels += l2
        line += '; end: left %d px%s, right %d px%s%s' % (w2[0], ' (%s)' % wh2[0] if w2[0] else '', w2[1],
                                                        ' (%s)' % wh2[1] if w2[1] else '',
                                                        '' if reached else ' (end not reached)')
    print(line, flush=True)
    sheet(shots, QA + '/edges_%d_%02d.png' % (ext, lv), cols=1, labels=labels)
