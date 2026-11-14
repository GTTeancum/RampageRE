"""Find compiled Xbox vertex shaders (header word 0x2078/0x7778/0x7378, word count, then 16-byte instructions
whose first dword is 0) in a file; print offset, kind, instruction count, and which input registers / constant
range each reads."""
import struct
import sys

d = open(sys.argv[1], 'rb').read()
KIND = {0x2078: 'xvs', 0x7778: 'xvsw', 0x7378: 'xvss'}
found = 0
for off in range(0, len(d) - 20, 4):
    t, n = struct.unpack_from('<HH', d, off)
    if t not in KIND or not 1 <= n <= 136 or off + 4 + 16 * n > len(d):
        continue
    ins = [struct.unpack_from('<4I', d, off + 4 + 16 * i) for i in range(n)]
    if any(i[0] != 0 for i in ins):
        continue
    if not ins[-1][3] & 1:          # the last instruction carries the end bit
        continue
    inputs, consts, arl = set(), set(), False
    for _, b, c, e in ins:
        opcode = (b >> 21) & 0xF
        if opcode == 0xD:
            arl = True
        consts.add((b >> 13) & 0xFF)
        inputs.add((b >> 9) & 0xF)
    found += 1
    print(f'{off:#x} {KIND[t]} {n} instructions  inputs v{sorted(inputs)}  '
          f'constants {min(consts)}..{max(consts)}  indexed-constants={"yes" if arl else "no"}')
print(found, 'shaders')
