"""Wrap an Xbox executable (XBE) in an ELF32 file so Ghidra or IDA load it with its sections and known names.

Each XBE section becomes a loadable segment and a section at its own address. Names come from a JSON of
{address: name} (tools/xbe_libmatch.py) and, for the kernel import table, from the XDK's xboxkrnl.lib.

Usage: python tools/xbe2elf.py XBE OUT.elf [--labels LABELS.json ...] [--kernel-lib xboxkrnl.lib]
"""
import json
import struct
import sys

KEYS = (0x5B6D40B6, 0xEFB1F152, 0x46437DCD)     # kernel thunk address: retail, debug, chihiro
ENTRY_KEYS = (0xA8FC57AB, 0x94859D4B, 0x40B5C16E)


def kernel_names(lib):
    """ordinal -> name from the short import objects of xboxkrnl.lib"""
    d = open(lib, 'rb').read()
    out, o = {}, 8
    while o + 60 <= len(d):
        size = int(d[o + 48:o + 58])
        body = d[o + 60:o + 60 + size]
        o += 60 + size + (size & 1)
        if len(body) >= 20 and body[:4] == b'\x00\x00\xff\xff':
            ordinal = struct.unpack_from('<H', body, 16)[0]
            name = body[20:body.index(b'\0', 20)].decode('latin1')
            out[ordinal] = name.lstrip('_@').split('@')[0]
    return out


def main():
    args = sys.argv[1:]
    xbe = open(args[0], 'rb').read()
    out = args[1]
    labels, klib = {}, None
    i = 2
    while i < len(args):
        if args[i] == '--labels':
            labels.update({int(k, 16): v for k, v in json.load(open(args[i + 1])).items()})
        elif args[i] == '--kernel-lib':
            klib = args[i + 1]
        i += 2
    base = struct.unpack_from('<I', xbe, 0x104)[0]
    nsec, secaddr = struct.unpack_from('<II', xbe, 0x11C)
    entry_enc, = struct.unpack_from('<I', xbe, 0x128)
    thunk_enc, = struct.unpack_from('<I', xbe, 0x158)
    secs = []
    for k in range(nsec):
        fl, va, vsz, raw, rsz, naddr = struct.unpack_from('<IIIIII', xbe, secaddr - base + k * 56)
        name = xbe[naddr - base:naddr - base + 16].split(b'\0')[0].decode('latin1')
        secs.append((name, va, vsz, xbe[raw:raw + rsz], fl))
    hdr_size = struct.unpack_from('<I', xbe, 0x108)[0]
    secs.insert(0, ('.xbehdr', base, hdr_size, xbe[:hdr_size], 0))

    def inside(a):
        return any(va <= a < va + vsz for _, va, vsz, _, _ in secs[1:])

    entry = next((entry_enc ^ k for k in ENTRY_KEYS if inside(entry_enc ^ k)), 0)
    thunk = next((thunk_enc ^ k for k in KEYS if inside(thunk_enc ^ k)), 0)
    if entry:
        labels.setdefault(entry, 'xbe_entry')
    if thunk:
        names = kernel_names(klib) if klib else {}
        sec = next(s for s in secs if s[1] <= thunk < s[1] + s[2])
        a = thunk
        while True:
            v = struct.unpack_from('<I', sec[3], a - sec[1])[0]
            if not v:
                break
            o = v & 0x7FFFFFFF
            labels[a] = '__imp_' + names.get(o, f'xboxkrnl_{o}')
            a += 4

    # layout: ehdr, phdrs, section data, strtabs, symtab, shdrs
    nph = len(secs)
    off = 52 + 32 * nph
    placed = []
    for name, va, vsz, data, fl in secs:
        off = (off + 0xFFF) & ~0xFFF
        off += va & 0xFFF                       # file offset congruent to the address
        placed.append(off)
        off += len(data)
    shstr = bytearray(b'\0')
    shname = []
    for name, *_ in secs:
        shname.append(len(shstr)); shstr += name.encode() + b'\0'
    extra = {}
    for n in ('.symtab', '.strtab', '.shstrtab'):
        extra[n] = len(shstr); shstr += n.encode() + b'\0'
    strtab = bytearray(b'\0')
    syms = [struct.pack('<IIIBBH', 0, 0, 0, 0, 0, 0)]
    locals_n = 1
    for a, n in sorted(labels.items()):
        idx = next((k + 1 for k, s in enumerate(secs) if s[1] <= a < s[1] + s[2]), None)
        if idx is None:
            continue
        code = secs[idx - 1][4] & 4
        syms.append(struct.pack('<IIIBBH', len(strtab), a, 0, (1 << 4) | (2 if code else 1), 0, idx))
        strtab += n.encode('latin1') + b'\0'
    symtab = b''.join(syms)
    off = (off + 3) & ~3
    strtab_off = off; off += len(strtab)
    off = (off + 3) & ~3
    symtab_off = off; off += len(symtab)
    shstr_off = off; off += len(shstr)
    off = (off + 3) & ~3
    shoff = off
    nsh = 1 + len(secs) + 3
    f = bytearray(shoff + 40 * nsh)
    f[:52] = struct.pack('<16sHHIIIIIHHHHHH', b'\x7fELF\x01\x01\x01' + bytes(9), 2, 3, 1, entry, 52, shoff, 0, 52, 32,
                         nph, 40, nsh, nsh - 1)
    for k, (name, va, vsz, data, fl) in enumerate(secs):
        flags = 4 | (1 if fl & 4 else 0) | (2 if fl & 1 else 0)
        f[52 + 32 * k:52 + 32 * k + 32] = struct.pack('<IIIIIIII', 1, placed[k], va, va, len(data), max(vsz, len(data)),
                                                       flags, 0x1000)
        f[placed[k]:placed[k] + len(data)] = data
    f[strtab_off:strtab_off + len(strtab)] = strtab
    f[symtab_off:symtab_off + len(symtab)] = symtab
    f[shstr_off:shstr_off + len(shstr)] = shstr
    sh = [struct.pack('<10I', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)]
    for k, (name, va, vsz, data, fl) in enumerate(secs):
        flags = 2 | (4 if fl & 4 else 0) | (1 if fl & 1 else 0)
        sh.append(struct.pack('<10I', shname[k], 1, flags, va, placed[k], len(data), 0, 0, 16, 0))
        if vsz > len(data) and False:
            pass
    sh.append(struct.pack('<10I', extra['.symtab'], 2, 0, 0, symtab_off, len(symtab), len(secs) + 2, locals_n, 4, 16))
    sh.append(struct.pack('<10I', extra['.strtab'], 3, 0, 0, strtab_off, len(strtab), 0, 0, 1, 0))
    sh.append(struct.pack('<10I', extra['.shstrtab'], 3, 0, 0, shstr_off, len(shstr), 0, 0, 1, 0))
    f[shoff:] = b''.join(sh)
    open(out, 'wb').write(f)
    print(f'{out}: {len(secs)} sections, {len(syms) - 1} symbols, entry {entry:#x}, kernel thunks at {thunk:#x}')


if __name__ == '__main__':
    main()
