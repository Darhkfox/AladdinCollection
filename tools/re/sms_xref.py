"""Find instructions that reference a RAM/ROM address (ld (nn),a / ld a,(nn) / ld rr,nn / ld (nn),rr ...).

    python sms_xref.py C120 [C11F ...]       -> every executed-or-not instruction in banks 0-1 (and the others,
                                                decoded linearly) whose operand is the address
"""
import sys
import sms_re as R
import sms_listing as L
import z80dis


def xrefs(target, banks=None):
    data = R.rom()
    lo = target & 0xFF
    hi = target >> 8
    out = []
    nb = len(data) // 0x4000
    for i in range(len(data) - 2):
        if data[i] == lo and data[i + 1] == hi:
            # try instruction starts 1..4 bytes back
            for back in (1, 2, 3):
                st = i - back
                if st < 0:
                    continue
                if banks is not None and st // 0x4000 not in banks:
                    continue
                org = L.org_of(st)
                base = st - org
                read = lambda z: data[(z + base) % len(data)]
                t, n = z80dis.disasm(read, org)
                if st + n == i + 2 and ('$%04X' % target) in t:
                    out.append((st, org, t))
    return out


if __name__ == '__main__':
    c = L.load_cov()
    for a in sys.argv[1:]:
        t = int(a.replace('$', ''), 16)
        print('== $%04X' % t)
        for st, org, txt in xrefs(t):
            print('  %05X %04X  %-26s %s' % (st, org, txt, '*' if c[st] else ''))
