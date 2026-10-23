"""Small Z80 disassembler (decoding by the x/y/z/p/q fields of the opcode, incl. CB/ED/DD/FD/DDCB/FDCB).

Usage:
    from z80dis import disasm, dis_range
    text, length = disasm(read, addr)        # read(addr) -> byte
    for addr, length, text in dis_range(read, start, end): ...

Command line (ROM offsets; the Z80 address shown is the offset mapped to slot `--slot` (0/1/2)):
    python tools/z80dis.py <rom> <start> <end> [--org $8000]
"""
import sys

R = ['b', 'c', 'd', 'e', 'h', 'l', '(hl)', 'a']
RP = ['bc', 'de', 'hl', 'sp']
RP2 = ['bc', 'de', 'hl', 'af']
CC = ['nz', 'z', 'nc', 'c', 'po', 'pe', 'p', 'm']
ALU = ['add a,', 'adc a,', 'sub ', 'sbc a,', 'and ', 'xor ', 'or ', 'cp ']
ROT = ['rlc', 'rrc', 'rl', 'rr', 'sla', 'sra', 'sll', 'srl']
IM = ['0', '0', '1', '2', '0', '0', '1', '2']
BLI = {(4, 0): 'ldi', (4, 1): 'cpi', (4, 2): 'ini', (4, 3): 'outi',
       (5, 0): 'ldd', (5, 1): 'cpd', (5, 2): 'ind', (5, 3): 'outd',
       (6, 0): 'ldir', (6, 1): 'cpir', (6, 2): 'inir', (6, 3): 'otir',
       (7, 0): 'lddr', (7, 1): 'cpdr', (7, 2): 'indr', (7, 3): 'otdr'}


def h8(v):
    return '$%02X' % (v & 0xFF)


def h16(v):
    return '$%04X' % (v & 0xFFFF)


def s8(v):
    return v - 256 if v & 0x80 else v


def disasm(read, addr):
    """-> (text, length) for the instruction at addr"""
    pc = addr
    op = read(pc)
    pc += 1
    ix = None
    if op in (0xDD, 0xFD):
        ix = 'ix' if op == 0xDD else 'iy'
        op = read(pc)
        pc += 1
        if op in (0xDD, 0xFD, 0xED):
            return ('db %s' % h8(read(addr)), 1)

    def imm8():
        nonlocal pc
        v = read(pc)
        pc += 1
        return v

    def imm16():
        nonlocal pc
        v = read(pc) | (read(pc + 1) << 8)
        pc += 2
        return v

    disp = [None]

    def reg(i, allow_hl_sub=True):
        # register i with IX/IY substitution
        if ix is None:
            return R[i]
        if i == 6:
            if disp[0] is None:
                disp[0] = s8(imm8())
            d = disp[0]
            return '(%s%s%s)' % (ix, '+' if d >= 0 else '-', h8(abs(d)))
        if allow_hl_sub and i == 4:
            return ix + 'h'
        if allow_hl_sub and i == 5:
            return ix + 'l'
        return R[i]

    def rp(i):
        return ix if (ix and i == 2) else RP[i]

    def rp2(i):
        return ix if (ix and i == 2) else RP2[i]

    if op == 0xCB:
        if ix:
            d = s8(read(pc))
            op2 = read(pc + 1)
            pc += 2
            x, y, z = op2 >> 6, (op2 >> 3) & 7, op2 & 7
            m = '(%s%s%s)' % (ix, '+' if d >= 0 else '-', h8(abs(d)))
            if x == 0:
                t = '%s %s' % (ROT[y], m)
            elif x == 1:
                t = 'bit %d,%s' % (y, m)
            elif x == 2:
                t = 'res %d,%s' % (y, m)
            else:
                t = 'set %d,%s' % (y, m)
            if x != 1 and z != 6:
                t += ',' + R[z]
            return (t, pc - addr)
        op2 = imm8()
        x, y, z = op2 >> 6, (op2 >> 3) & 7, op2 & 7
        if x == 0:
            return ('%s %s' % (ROT[y], R[z]), pc - addr)
        return ('%s %d,%s' % (['', 'bit', 'res', 'set'][x], y, R[z]), pc - addr)

    if op == 0xED:
        op2 = imm8()
        x, y, z = op2 >> 6, (op2 >> 3) & 7, op2 & 7
        p, q = y >> 1, y & 1
        t = None
        if x == 1:
            if z == 0:
                t = 'in (c)' if y == 6 else 'in %s,(c)' % R[y]
            elif z == 1:
                t = 'out (c),0' if y == 6 else 'out (c),%s' % R[y]
            elif z == 2:
                t = '%s hl,%s' % ('sbc' if q == 0 else 'adc', RP[p])
            elif z == 3:
                n = imm16()
                t = 'ld (%s),%s' % (h16(n), RP[p]) if q == 0 else 'ld %s,(%s)' % (RP[p], h16(n))
            elif z == 4:
                t = 'neg'
            elif z == 5:
                t = 'reti' if y == 1 else 'retn'
            elif z == 6:
                t = 'im ' + IM[y]
            else:
                t = ['ld i,a', 'ld r,a', 'ld a,i', 'ld a,r', 'rrd', 'rld', 'nop', 'nop'][y]
        elif x == 2 and z <= 3 and y >= 4:
            t = BLI[(y, z)]
        if t is None:
            t = 'db $ED,%s' % h8(op2)
        return (t, pc - addr)

    x, y, z = op >> 6, (op >> 3) & 7, op & 7
    p, q = y >> 1, y & 1
    t = None
    if x == 0:
        if z == 0:
            if y == 0:
                t = 'nop'
            elif y == 1:
                t = "ex af,af'"
            else:
                d = s8(imm8())
                tgt = h16(pc + d)
                t = 'djnz ' + tgt if y == 2 else 'jr ' + tgt if y == 3 else 'jr %s,%s' % (CC[y - 4], tgt)
        elif z == 1:
            if q == 0:
                t = 'ld %s,%s' % (rp(p), h16(imm16()))
            else:
                t = 'add %s,%s' % (rp(2), rp(p))
        elif z == 2:
            if q == 0:
                t = ['ld (bc),a', 'ld (de),a', None, None][p]
                if p == 2:
                    t = 'ld (%s),%s' % (h16(imm16()), rp(2))
                elif p == 3:
                    t = 'ld (%s),a' % h16(imm16())
            else:
                t = ['ld a,(bc)', 'ld a,(de)', None, None][p]
                if p == 2:
                    t = 'ld %s,(%s)' % (rp(2), h16(imm16()))
                elif p == 3:
                    t = 'ld a,(%s)' % h16(imm16())
        elif z == 3:
            t = '%s %s' % ('inc' if q == 0 else 'dec', rp(p))
        elif z == 4:
            t = 'inc ' + reg(y)
        elif z == 5:
            t = 'dec ' + reg(y)
        elif z == 6:
            r = reg(y)
            t = 'ld %s,%s' % (r, h8(imm8()))
        else:
            t = ['rlca', 'rrca', 'rla', 'rra', 'daa', 'cpl', 'scf', 'ccf'][y]
    elif x == 1:
        if z == 6 and y == 6:
            t = 'halt'
        else:
            # with (ix+d) the other operand is the plain register
            if ix and (y == 6 or z == 6):
                t = 'ld %s,%s' % (reg(y, False), reg(z, False))
            else:
                t = 'ld %s,%s' % (reg(y), reg(z))
    elif x == 2:
        t = ALU[y] + reg(z)
    else:
        if z == 0:
            t = 'ret ' + CC[y]
        elif z == 1:
            if q == 0:
                t = 'pop ' + rp2(p)
            else:
                t = ['ret', 'exx', 'jp (%s)' % rp(2), 'ld sp,%s' % rp(2)][p]
        elif z == 2:
            t = 'jp %s,%s' % (CC[y], h16(imm16()))
        elif z == 3:
            if y == 0:
                t = 'jp ' + h16(imm16())
            elif y == 2:
                t = 'out (%s),a' % h8(imm8())
            elif y == 3:
                t = 'in a,(%s)' % h8(imm8())
            elif y == 4:
                t = 'ex (sp),' + rp(2)
            elif y == 5:
                t = 'ex de,hl'
            elif y == 6:
                t = 'di'
            elif y == 7:
                t = 'ei'
        elif z == 4:
            t = 'call %s,%s' % (CC[y], h16(imm16()))
        elif z == 5:
            if q == 0:
                t = 'push ' + rp2(p)
            elif p == 0:
                t = 'call ' + h16(imm16())
        elif z == 6:
            t = ALU[y] + h8(imm8())
        else:
            t = 'rst ' + h8(y * 8)
    return (t, pc - addr)


def dis_range(read, start, end):
    a = start
    while a < end:
        t, n = disasm(read, a)
        yield a, n, t
        a += n


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    ap.add_argument('start')
    ap.add_argument('end')
    ap.add_argument('--org', default=None, help='Z80 address of the start offset (default: offset & $3FFF | $8000 above $8000)')
    a = ap.parse_args()
    data = open(a.rom, 'rb').read()
    num = lambda s: int(s.replace('$', '0x'), 0)
    start, end = num(a.start), num(a.end)
    org = num(a.org) if a.org else (start if start < 0x8000 else 0x8000 | (start & 0x3FFF))
    base = start - org
    read = lambda z: data[(z + base) % len(data)]
    for addr, n, t in dis_range(read, org, org + (end - start)):
        b = ' '.join('%02X' % read(addr + i) for i in range(n))
        print('%05X %04X  %-12s %s' % (addr + base, addr, b, t))


if __name__ == '__main__':
    main()
