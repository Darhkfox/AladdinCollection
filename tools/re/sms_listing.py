"""Coverage-guided disassembly listing of the ROM.

    python sms_listing.py cov <state|boot> <frames> [buttons-script]   -> adds coverage to sms_out/coverage.bin
    python sms_listing.py list [bank...]                                -> sms_out/bank_XX.asm

Coverage is kept per ROM offset (instruction starts executed, summed over runs). The listing disassembles
executed code from each executed instruction start and shows the rest as `db` lines (16 bytes each), so data
tables are not mis-decoded as code. Z80 addresses: bank 0 at $0000, 1 at $4000, others shown at $8000 (slot 2).
"""
import os
import sys
import random
import struct
import sms_re as R
import z80dis

COV = os.path.join(R.OUT, 'coverage.bin')


def load_cov():
    n = len(R.rom())
    if os.path.exists(COV):
        d = open(COV, 'rb').read()
        return list(struct.unpack('<%dI' % n, d))
    return [0] * n


def save_cov(c):
    with open(COV, 'wb') as f:
        f.write(struct.pack('<%dI' % len(c), *c))


def add_run(s, frames, script=None, seed=1):
    """runs `frames` frames with coverage on; script(s, i) may set buttons each frame"""
    s.coverage_on(True)
    rng = random.Random(seed)
    for i in range(frames):
        if script:
            script(s, i, rng)
        s.run(1)
    cov = s.coverage()
    c = load_cov()
    for i in range(len(c)):
        if cov[i]:
            c[i] += cov[i]
    s.coverage_on(False)
    save_cov(c)


def random_play(s, i, rng):
    if i % 8 == 0:
        s.hold(rng.choice(['RIGHT', 'RIGHT+1', 'RIGHT+2', 'LEFT', '1', '2', 'UP', 'DOWN', 'RIGHT', 'RIGHT+2', 0]))


def org_of(off):
    b = off // 0x4000
    if b == 0:
        return off
    if b == 1:
        return off
    return 0x8000 | (off & 0x3FFF)


def listing(bank):
    data = R.rom()
    c = load_cov()
    lo, hi = bank * 0x4000, bank * 0x4000 + 0x4000
    base = lo - org_of(lo)
    read = lambda z: data[(z + base) % len(data)]
    lines = []
    off = lo
    while off < hi:
        if c[off]:
            t, n = z80dis.disasm(read, off - base)
            lines.append('%05X %04X  %-12s %-28s ; %d' % (off, off - base, ' '.join('%02X' % data[off + i] for i in range(n)), t, c[off]))
            off += n
        else:
            e = off
            while e < hi and not c[e] and e - off < 16:
                e += 1
            lines.append('%05X %04X  db %s' % (off, off - base, ','.join('%02X' % data[i] for i in range(off, e))))
            off = e
    p = os.path.join(R.OUT, 'bank_%02X.asm' % bank)
    with open(p, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    return p


def show(off, n=60, force=False):
    """prints n lines of the listing from ROM offset `off` (force: disassemble even if not executed)"""
    data = R.rom()
    c = load_cov()
    base = off - org_of(off)
    read = lambda z: data[(z + base) % len(data)]
    a = off
    for _ in range(n):
        if c[a] or force:
            t, k = z80dis.disasm(read, a - base)
            print('%05X %04X  %-12s %-28s ; %d' % (a, a - base, ' '.join('%02X' % data[a + i] for i in range(k)), t, c[a]))
            a += k
        else:
            e = a
            while e < len(data) and not c[e] and e - a < 16:
                e += 1
            print('%05X %04X  db %s' % (a, a - base, ','.join('%02X' % data[i] for i in range(a, e))))
            a = e


if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'show':
        show(int(sys.argv[2].replace('$', '0x'), 0), int(sys.argv[3]) if len(sys.argv) > 3 else 60,
             len(sys.argv) > 4)
    elif cmd == 'list':
        banks = [int(x, 0) for x in sys.argv[2:]] or range(len(R.rom()) // 0x4000)
        for b in banks:
            print(listing(b))
