"""Disassemble compiled Xbox (NV2A) vertex shaders. usage: python vshdis.py FILE OFFSET [OFFSET...]"""
import struct
import sys

MAC = ['NOP', 'MOV', 'MUL', 'ADD', 'MAD', 'DP3', 'DPH', 'DP4', 'DST', 'MIN', 'MAX', 'SLT', 'SGE', 'ARL', '?', '?']
ILU = ['NOP', 'MOV', 'RCP', 'RCC', 'RSQ', 'EXP', 'LOG', 'LIT']
OUT = ['oPos', '?', '?', 'oD0', 'oD1', 'oFog', 'oPts', 'oB0', 'oB1', 'oT0', 'oT1', 'oT2', 'oT3', '?', '?', 'a0.x']
XYZW = 'xyzw'


def f(w, i, lo, n):
    return (w[i] >> lo) & ((1 << n) - 1)


def mask(m):
    return ''.join(c for c, b in zip(XYZW, (8, 4, 2, 1)) if m & b)


def src(w, neg, swz, reg, mux, a0x):
    s = ''.join(XYZW[x] for x in swz)
    if s == 'xyzw':
        s = ''
    elif len(set(s)) == 1:
        s = s[0]
    if mux == 1:
        r = f'r{reg}'
    elif mux == 2:
        r = f'v{f(w, 1, 9, 4)}'
    else:
        c = f(w, 1, 13, 8)
        r = f'c[a0.x+{c}]' if a0x else f'c[{c}]'
    return ('-' if neg else '') + r + ('.' + s if s else '')


def dis(d, off):
    t, n = struct.unpack_from('<HH', d, off)
    print(f'--- shader at {off:#x}: {n} instructions')
    for i in range(n):
        w = struct.unpack_from('<4I', d, off + 4 + 16 * i)
        a0x = f(w, 3, 1, 1)
        a = src(w, f(w, 1, 8, 1), [f(w, 1, 6, 2), f(w, 1, 4, 2), f(w, 1, 2, 2), f(w, 1, 0, 2)], f(w, 2, 28, 4),
                f(w, 2, 26, 2), a0x)
        b = src(w, f(w, 2, 25, 1), [f(w, 2, 23, 2), f(w, 2, 21, 2), f(w, 2, 19, 2), f(w, 2, 17, 2)], f(w, 2, 13, 4),
                f(w, 2, 11, 2), a0x)
        c = src(w, f(w, 2, 10, 1), [f(w, 2, 8, 2), f(w, 2, 6, 2), f(w, 2, 4, 2), f(w, 2, 2, 2)],
                (f(w, 2, 0, 2) << 2) | f(w, 3, 30, 2), f(w, 3, 28, 2), a0x)
        mac, ilu = f(w, 1, 21, 4), f(w, 1, 25, 3)
        mm, im, om = f(w, 3, 24, 4), f(w, 3, 16, 4), f(w, 3, 12, 4)
        outr, addr, orb, omux = f(w, 3, 20, 4), f(w, 3, 3, 8), f(w, 3, 11, 1), f(w, 3, 2, 1)
        dests = []
        if mac:
            nargs = {1: 1, 13: 1, 4: 3}.get(mac, 2)
            args = [a, b, c][:nargs] if mac != 3 else [a, c]
            if mac == 13:
                dests.append(f'ARL a0.x, {a}')
            else:
                tg = []
                if mm:
                    tg.append(f'r{outr}.{mask(mm)}')
                if om and omux == 0:
                    tg.append((OUT[addr & 15] if orb else f'c[{addr}]') + '.' + mask(om))
                dests.append(f'{MAC[mac]} {", ".join(tg) or "-"} <- {", ".join(args)}')
        if ilu:
            tg = []
            if im:
                tg.append(f'r{1 if mac else outr}.{mask(im)}')
            if om and omux == 1:
                tg.append((OUT[addr & 15] if orb else f'c[{addr}]') + '.' + mask(om))
            dests.append(f'{ILU[ilu]} {", ".join(tg) or "-"} <- {c}')
        print(f'  {i:2d}: ' + ' ; '.join(dests))


d = open(sys.argv[1], 'rb').read()
for o in sys.argv[2:]:
    dis(d, int(o, 16))
