"""The raster bands' lines in widescreen against the game's own band routines:
python snes_bands.py <ext> [levels] [turbo]

The levels whose background the game scrolls in raster bands built by world y ($083F $0A: Agrabah Market, the Market at
Dusk; $0E: Agrabah Rooftops; `levels` defaults to 0,2,3) are toured with Aladdin carried to points all over them (both
ends, top to bottom, the middle), walking and jumping in between (invincible). Per frame, on every line of the picture:

  rows : the module's BG2 shift (Ppu::bgHofsAdj, snt.py line_regs) against the one the game's own routine gives with
         its camera at the picture's middle v (`expected`): the line's band from its HDMA table as the game builds it
         ($81:C84B: the first entry one line, then the bands by world y from camera y + 8) and the line HDMA is switched
         on at this frame (the IRQ's STA $420C, $80:8345: the table's second entry reaches the screen 3 rows later),
         its value as the routine works it out ($81:C952 / $81:C98C) - independent of the module's own model. A line
         whose register the routine doesn't explain (a frame the logic didn't finish in, a shake) is skipped and
         counted. Mode $0A's first four bands (the ratchet) are left to snes_motion.py.
  still: where the view stops at a level's end, the lines of the bands that follow the camera must hold still while
         the camera moves (the view's left edge the same): pixels of those lines changed from the last frame (BG2 only,
         not in a fade).
  edge : where the view is centred, the picture against the bare core's (the same state, the extension drawn from the
         VRAM map at the game's own registers, the map filled with the background's 512-px repeat first: the game
         leaves a column it never shows stale), every 10 frames: pixels that differ (BG2 only; mode $0A's ratchet
         bands, which keep the virtual camera's lead, and each line's margin past the level's end, mirrored and
         dimmed, left out: where the module, run again from the frame's state with the mirror off, draws it black).
The tour also carries Aladdin across both ends at each height, the camera passing where the view stops.
`turbo` 4: Remove Slowdown on. Writes tools/qa/snes/bands_<ext>.txt and a sheet of the stopped view's frames
(tools/qa/snes/bands_<ext>_<level>.png; BANDS_DEBUG=<n>: the first n edge differences side by side)."""
import os
import sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snes_common import *  # noqa

ext = int(sys.argv[1]) if len(sys.argv) > 1 else 106
levels = [int(a) for a in sys.argv[2].split(',')] if len(sys.argv) > 2 and sys.argv[2] != 'all' else [0, 2, 3]
turbo = int(sys.argv[3]) if len(sys.argv) > 3 else 1
HDMA_ON = 0x808345


def unwrap(reg, near):
    return near + ((((reg - near) & 0x3FF) + 0x200) & 0x3FF) - 0x200


def fshr(v, k):
    return v >> k   # (Python's >> floors)


def routine(s, mode):
    """Per band: ('t', shift) a shift of the drifting sum $1A30 + camera, ('c', f) a function of the camera, or
    ('k', shift) mode $0A's ratchet - as the band routines write $7E:F600 + 2k."""
    idx = 2 if mode == 0x0A else 0
    n = s.r16(0x80C602 + idx) >> 1
    base = 0x80C606 + s.r16(0x80C606 + idx)
    bounds = [s.r16(base + 2 * k) for k in range(n + 1)]
    if mode == 0x0A:   # $81:C98C: S, S/2, S/4, S/8, then camera / 2, camera * 5/4
        kinds = [('k', 0), ('k', 1), ('k', 2), ('k', 3), ('c', lambda c: c >> 1), ('c', lambda c: c + (c >> 2))]
    else:              # $81:C952: camera / 2^(n-k) from the bottom band up, then bands 0, 1: (S) / 2, / 4
        kinds = [('c', (lambda j: lambda c: c >> j)(n - k)) for k in range(n)]
        kinds[0], kinds[1] = ('t', 1), ('t', 2)
    return n, bounds, kinds


def tile_at(s, off, bank, x, y):
    """BG2's tile entry at world (x, y) from the level's data, as the column builder ($81:C2E0) finds it."""
    idx = off + (((y + 256) & 0xFF00) >> 2) + ((((x >> 8) + 1) * 2) & 0xFE)
    pg = s.r16(0x7FF000 + (idx & 0xFFF))
    if pg & 0x8000:
        pg = 0
    blk = s.r16(0x7F0000 + ((pg + (((x >> 3) & 0x1E) | ((y << 1) & 0x1E0))) & 0xFFFF)) | 0x8000
    return s.r16((bank << 16) | ((blk + (((x >> 2) & 2) | ((y >> 1) & 4))) & 0xFFFF))


def fill_map(s):
    """BG2's whole 64x32 VRAM map from the background's first 512 px (its repeat), for the rows on screen: what the
    map holds where the game has streamed it (the column before x = 0 keeps the border column's page from the
    level's start, which the game's own screen never shows)."""
    base, off, bank = s.r16(0x188E + 0x2E), s.r16(0x188E + 0x42), s.r8(0x188E + 0x4C)
    y0 = s.s16(0x188E + 0x3F)
    for cy in range(32):
        y = y0 + ((cy * 8 - y0) & 0xFF)
        for cx in range(64):
            a = base + (0x400 if cx >= 32 else 0) + cy * 32 + (cx & 31)
            s.vram[a & 0x7FFF] = tile_at(s, off, bank, cx * 8, y)


def tour(maxx, maxy):
    """Per frame: Aladdin's position to write (or None) and the buttons. Carried to points at both ends and the middle,
    heights over the level's camera range, walking and jumping from each; then carried across both ends at each
    height (the camera moving through where the view stops, its y steady)."""
    ys = sorted({0x60, min(maxy + 0xA0, 0x60 + maxy // 2), maxy + 0xA0})
    right = maxx + 256
    xs = [40, 110, 170, 260, maxx // 2, right - 300, right - 180, right - 100, right - 40]
    for x in xs:
        for y in (ys if x in (40, 110, 170, right - 40, right - 100) else ys[:1] + ys[-1:]):
            for g in range(90):
                if g < 25:
                    yield (x, y), 0
                else:
                    d = 'RIGHT' if x < maxx // 2 + 128 else 'LEFT'
                    if (g // 20) % 2:
                        d = 'LEFT' if d == 'RIGHT' else 'RIGHT'
                    yield None, d + ('+B' if g % 23 == 0 else '')
    for y in ys:
        for x0, x1 in ((40, 400), (400, 40), (right - 40, right - 400), (right - 400, right - 40)):
            for g in range(20):
                yield (x0, y), 0
            for x in range(x0, x1, 2 if x1 > x0 else -2):
                yield (x, y), 0


out = []
for lv in levels:
    w = SNES()
    w.module('snes')
    w.configure(ext, True, 'invincible,inf_lives')
    if turbo > 1:
        w.turbo(turbo)
    w.layers(2)
    w.start_level(lv)
    w.run(30)
    b = SNES()
    b.layers(2)
    b.set_ext(ext, ext)
    # the module again with the level black past its ends (not mirrored): where each line's level ends
    m = SNES()
    m.module('snes')
    m.configure(ext, True, 'invincible,inf_lives')
    if turbo > 1:
        m.turbo(turbo)
    m.layers(2)
    m.edge_mirror(False)
    mode = w.r16(0x83F)
    if mode not in (0x0A, 0x0E):
        out.append('level %d: mode %02X has no bands by world y' % (lv, mode))
        continue
    n, bounds, kinds = routine(w, mode)
    maxx, maxy = w.s16(0x1A04), w.s16(0x1A06)
    bad = {'rows': 0, 'still': 0, 'edge': 0}
    cnt = {'rows': 0, 'skipped': 0, 'still_frames': 0, 'edge_frames': 0, 'frames': 0}
    notes = []
    prev_pic, prev_el, prev_camy, prev_lit, prev_c = None, None, None, False, None
    shots, labels = [], []
    level = w.r8(0x35E)
    w.bp(HDMA_ON)
    f = 0
    for pos, buttons in tour(maxx, maxy):
        if w.r8(0x35E) != level:
            break
        if pos:
            w.w16(0x8E7, pos[0])
            w.w16(0x8EA, pos[1])
        w.hold(buttons)
        state = w.save() if f % 10 == 0 else None
        w.run(1)
        f += 1
        hits = w.bp_hits()
        el = w.view_left()
        if el is None or not w.info()['gameplay'] or w.ppu_regs()['bright'] == 0:
            prev_pic = None
            continue
        cnt['frames'] += 1
        regs = w.line_regs()
        c = unwrap(regs[100][0], w.s16(0x19ED))
        camy = unwrap(regs[100][3], w.s16(0x1777))
        v = c - el + ext
        row0 = hits[-1]['line'] + 3 if hits else 1 << 30
        band_of_row = []
        for r in range(224):
            yy = camy + 8 + max(0, r - row0)
            k = 0
            while k + 1 < n and yy >= bounds[k + 1]:
                k += 1
            band_of_row.append(k)
            kind, p = kinds[k]
            h, adj = regs[r][2] & 0x3FF, regs[r][5]
            if kind == 'k':
                continue
            if kind == 'c':
                if (p(c) & 0x3FF) != h:
                    cnt['skipped'] += 1
                    continue
                ok = {p(v) - p(c) - (v - c)}
            else:
                ok = {fshr(low + v - c, p) - (v - c) for low in range(1 << p)}
            cnt['rows'] += 1
            if adj not in ok:
                bad['rows'] += 1
                if len(notes) < 40:
                    notes.append('f%d row %d band %d (cam %d,%d v %d hdma line %s): adj %d, expected %s'
                                 % (f, r, k, c, camy, v, hits[-1]['line'] if hits else '-', adj, sorted(ok)))
        pic = grab_np(w)
        stopped = el != ext
        pr = w.ppu_regs()
        lit = pr['bright'] == 15 and not pr['fblank']   # (a fade, a life lost: the picture changes as a whole)
        if lit and prev_lit and prev_pic is not None and c != prev_c and c - el == prev_c - prev_el \
                and prev_camy == camy:
            # the camera moved, the view didn't (it stops at a level's end): the camera's bands hold still
            rows = [r for r in range(224) if kinds[band_of_row[r]][0] == 'c']
            if rows:
                cnt['still_frames'] += 1
                d = int((np.abs(pic[rows].astype(int) - prev_pic[rows].astype(int)).sum(axis=2) > 0).sum())
                if d:
                    bad['still'] += 1
                    if len(notes) < 80:
                        notes.append('f%d still: cam %d,%d el %d->%d: %d px moved' % (f, c, camy, prev_el, el, d))
                if len(shots) < 8 and (f % 4 == 0):
                    from PIL import Image
                    shots.append(Image.fromarray(pic))
                    labels.append('f%d cam %d,%d el %d' % (f, c, camy, el))
        if state is not None and not stopped and lit:
            b.load(state)
            fill_map(b)
            b.hold(0)
            b.run(1)
            ref = grab_np(b)
            m.load(state)
            m.hold(buttons)
            m.run(1)
            nm = grab_np(m)
            if ref.shape == pic.shape:
                cnt['edge_frames'] += 1
                # (past the level's end on a line - the line's page row - the module mirrors the level about its edge
                # and dims it: black there with the mirror off)
                a, r_ = pic.astype(int), ref.astype(int)
                differ = np.abs(r_ - a).sum(axis=2) > 0
                differ[(nm == 0).all(axis=2) & (pic != 0).any(axis=2)] = False
                # (mode $0A's ratchet bands keep the virtual camera's lead, band_d_: not the game's own place)
                differ[[r for r in range(224) if kinds[band_of_row[r]][0] == 'k']] = False
                d = int(differ.sum())
                if d:
                    bad['edge'] += 1
                    if len(notes) < 120:
                        notes.append('f%d edge: cam %d,%d: %d px differ from the bare core' % (f, c, camy, d))
                    if os.environ.get('BANDS_DEBUG') and bad['edge'] <= int(os.environ['BANDS_DEBUG']):
                        from PIL import Image
                        dm = np.zeros_like(pic)
                        dm[differ] = (255, 0, 0)
                        sheet([Image.fromarray(pic), Image.fromarray(ref), Image.fromarray(dm)],
                              QA + '/bands_edge_%d_%d_f%d.png' % (ext, lv, f), cols=1,
                              labels=['module', 'bare core (VRAM)', 'differs'])
        prev_pic, prev_el, prev_camy, prev_lit, prev_c = pic, el, camy, lit, c
    line = ('level %d mode %02X ext %d turbo %d: %d frames; rows %d bad of %d (%d skipped); still %d bad of %d; '
            'edge %d bad of %d' % (lv, mode, ext, turbo, cnt['frames'], bad['rows'], cnt['rows'], cnt['skipped'],
                                   bad['still'], cnt['still_frames'], bad['edge'], cnt['edge_frames']))
    print(line, flush=True)
    out.append(line)
    out += ['   ' + x for x in notes]
    if shots:
        sheet(shots, QA + '/bands_%d_%d.png' % (ext, lv), cols=2, labels=labels)
open(QA + '/bands_%d.txt' % ext, 'w').write('\n'.join(out) + '\n')
