"""The Market's pursuer (stage 0's second figure, bank $0C $A978; docs/re_sms_wide.md section 3) at 4:3 and wide: his
screen X ($C689) against Aladdin's ($C402) over the stage, his stuns ($C69D, count $C69E: the rolling barrels, type 8),
jumps ($C699) and when he catches Aladdin ($C438 = $FF written by $B74E).

    python sms_pursuer.py [--exts 0,22,74,102] [--frames 3700] [--policies run,pause,still,left,player] [--csv dir]

Policies: run (Right held, a jump every 50 frames of 200), pause (Right 90 frames, still 60), still, left (Left held),
player (sms_wide_shots.Player, invincible only). Per run: frames, stuns (camera, frames until he is back at his place,
frames of them he trails in the view left of the 4:3 picture, his lowest x), jumps, his distance to Aladdin while he
chases, when he catches Aladdin.
"""
import argparse
import os
from concurrent.futures import ProcessPoolExecutor
import sms_re as R


def s16(v):
    return v - 65536 if v & 0x8000 else v


def pad(policy, f):
    if policy == 'run':
        return 'RIGHT' if (f // 50) % 4 != 3 else 'RIGHT+2'
    if policy == 'pause':
        return 'RIGHT' if f % 150 < 90 else None
    if policy == 'left':
        return 'LEFT'
    return None


def run(job):
    E, cheats, policy, frames = job
    from sms_wide_shots import Player
    s = R.boot()
    s.module('sms')
    s.configure(E, True, '' if cheats == '-' else cheats)
    s.set_ext(E)
    s.start_level(0)
    s.watch(0xC438)
    p = Player(s, 0, retry=False) if policy == 'player' else None
    rows = []
    for f in range(frames):
        if p:
            p.step()
        else:
            s.mframes(1, pad(policy, f))
        mode = s.ram8(0xC200)
        rows.append({'frame': f, 'cam': s.ram16(0xC482), 'al_x': s16(s.ram16(0xC402)), 'x': s16(s.ram16(0xC689)),
                     'stunned': s.ram8(0xC69D), 'stuns': s.ram8(0xC69E), 'jumping': s.ram8(0xC699),
                     'caught': s.ram8(0xC438), 'mode': mode, 'figure': s.ram8(0xC698)})
        if mode != 2:
            break
    writers = sorted(set('%04X' % pc for fr, pc, ad, ln, v in s.watch_log() if v == 0xFF))
    return job, rows, writers


def summary(job, rows, writers):
    E, cheats, policy, _ = job
    stuns = []
    for i in range(1, len(rows)):
        if rows[i]['stuns'] > rows[i - 1]['stuns']:
            tgt = rows[i - 1]['x']
            j = i
            while j < len(rows) and (rows[j]['stunned'] or rows[j]['x'] < tgt):
                j += 1
            vis = sum(1 for r in rows[i:j] if -E - 24 < r['x'] < 0)
            stuns.append((rows[i]['cam'], j - i, vis, min(r['x'] for r in rows[i:j])))
    jumps = sum(1 for a, b in zip(rows, rows[1:]) if b['jumping'] and not a['jumping'])
    chase = [r['al_x'] - r['x'] for r in rows if r['figure'] == 1 and not r['stunned'] and not r['jumping']]
    caught = next((r['frame'] for r in rows if r['caught'] == 0xFF), None)
    return ('E=%3d %-10s %-6s: %4d frames, stuns (camera, frames until back, of them trailing in view, lowest x) %s, '
            'jumps %d, distance while chasing %.1f, caught at frame %s by %s' % (
                E, cheats, policy, len(rows), stuns, jumps, sum(chase) / max(1, len(chase)), caught, writers or '-'))


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--exts', default='0,22,74,102')
    ap.add_argument('--frames', type=int, default=3700)
    ap.add_argument('--policies', default='run,pause,still,left,player')
    ap.add_argument('--cheats', default='-,invincible')
    ap.add_argument('--csv', default='')
    a = ap.parse_args()
    jobs = [(int(E), ch, pol, a.frames) for ch in a.cheats.split(',') for pol in a.policies.split(',')
            for E in a.exts.split(',') if not (pol == 'player' and ch == '-')]
    with ProcessPoolExecutor(min(16, len(jobs))) as ex:
        res = list(ex.map(run, jobs))
    for job, rows, writers in res:
        print(summary(job, rows, writers), flush=True)
        if a.csv:
            os.makedirs(a.csv, exist_ok=True)
            E, ch, pol, _ = job
            with open(os.path.join(a.csv, 'pursuer_e%d_%s_%s.csv' % (E, ch.strip('-') or 'none', pol)), 'w') as fo:
                fo.write(','.join(rows[0]) + '\n')
                for r in rows:
                    fo.write(','.join(str(v) for v in r.values()) + '\n')
