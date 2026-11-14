"""Label the XDK library functions inside an Xbox executable by matching the XDK's own .lib files against it.

An XBE links Direct3D, DirectSound, XAPI and the C runtime statically, with no symbols. The libraries of the same
XDK build hold every function as COFF object code with relocations; a function's bytes, its relocated fields left
out, are searched in the XBE's sections. A match at exactly one address names that address.

Usage: python tools/xbe_libmatch.py XBE OUT.json LIB [LIB ...]
OUT.json: {"0x00012340": "_D3DDevice_SetVertexShader@4", ...} plus "ambiguous" counts on stderr.
"""
import json
import struct
import sys


def xbe_sections(d):
    base = struct.unpack_from('<I', d, 0x104)[0]
    nsec, secaddr = struct.unpack_from('<II', d, 0x11C)
    out = []
    for i in range(nsec):
        fl, va, vsz, raw, rsz, naddr = struct.unpack_from('<IIIIII', d, secaddr - base + i * 56)
        name = d[naddr - base:naddr - base + 16].split(b'\0')[0].decode('latin1')
        out.append((name, va, vsz, raw, rsz, fl))
    return out


def archive_members(d):
    if d[:8] != b'!<arch>\n':
        raise ValueError('not an archive')
    o, longnames = 8, b''
    while o + 60 <= len(d):
        hdr = d[o:o + 60]
        name = hdr[:16].decode('latin1').rstrip()
        size = int(hdr[48:58])
        body = d[o + 60:o + 60 + size]
        o += 60 + size + (size & 1)
        if name == '/':
            continue
        if name == '//':
            longnames = body
            continue
        if name.startswith('/') and name[1:].isdigit():
            at = int(name[1:])
            name = longnames[at:longnames.index(b'\0', at)].decode('latin1')
        yield name.rstrip('/'), body


def coff_functions(obj):
    """(symbol name, code bytes, mask) for every symbol defined in a code section of a COFF object."""
    if len(obj) < 20:
        return
    machine, nsec, _, symoff, nsym, opt, _ = struct.unpack_from('<HHIIIHH', obj, 0)
    if machine != 0x14C or not symoff or opt:
        return
    strtab = symoff + 18 * nsym

    def symname(raw):
        if raw[:4] == b'\0\0\0\0':
            at = strtab + struct.unpack_from('<I', raw, 4)[0]
            return obj[at:obj.index(b'\0', at)].decode('latin1')
        return raw.split(b'\0')[0].decode('latin1')

    secs = []
    for i in range(nsec):
        s = 20 + 40 * i
        vsz, va, rsz, raw, rel, _, nrel, _, ch = struct.unpack_from('<IIIIIIHHI', obj, s + 8)
        secs.append((raw, rsz, rel, nrel, ch))
    per = {}
    i = 0
    while i < nsym:
        raw = obj[symoff + 18 * i:symoff + 18 * i + 18]
        value, sec, typ, cls, naux = struct.unpack_from('<IhHBB', raw, 8)
        if sec > 0 and cls in (2, 3) and sec <= nsec and secs[sec - 1][4] & 0x20:
            name = symname(raw[:8])
            if not name.startswith('.') and not name.startswith('$'):
                per.setdefault(sec, []).append((value, name, cls))
        i += 1 + naux
    for sec, syms in per.items():
        raw, rsz, rel, nrel, ch = secs[sec - 1]
        if not raw or not rsz:
            continue
        code = obj[raw:raw + rsz]
        mask = bytearray(b'\xff' * rsz)
        for k in range(nrel):
            va, _, rtype = struct.unpack_from('<IIH', obj, rel + 10 * k)
            if rtype in (6, 7, 0x14, 0xB, 0xA) and va + 4 <= rsz:   # DIR32, DIR32NB, REL32, SECREL, SECTION
                mask[va:va + 4] = b'\0\0\0\0'
        syms.sort()
        for n, (value, name, cls) in enumerate(syms):
            end = syms[n + 1][0] if n + 1 < len(syms) else rsz
            if end - value >= 8:
                yield name, code[value:end], bytes(mask[value:end])


def anchor(code, mask, want=24):
    """The longest run of fixed bytes (start, length), capped at `want`."""
    best, run = (0, 0), 0
    for i, m in enumerate(mask):
        run = run + 1 if m else 0
        if run > best[1]:
            best = (i - run + 1, run)
            if run >= want:
                break
    return best


def main():
    xbe = open(sys.argv[1], 'rb').read()
    out = sys.argv[2]
    sections = [(n, va, xbe[raw:raw + rsz]) for n, va, vsz, raw, rsz, fl in xbe_sections(xbe) if fl & 4]  # executable
    funcs = {}
    for lib in sys.argv[3:]:
        for member, body in archive_members(open(lib, 'rb').read()):
            for name, code, mask in coff_functions(body):
                # trailing padding (int3 / nop) is not part of the function
                n = len(code)
                while n > 8 and code[n - 1] in (0xCC, 0x90) and mask[n - 1]:
                    n -= 1
                key = (code[:n], mask[:n])
                funcs.setdefault(key, set()).add(name)
    labels, ambiguous, tried = {}, 0, 0
    for (code, mask), names in funcs.items():
        fixed = sum(1 for m in mask if m)
        if fixed < 10:
            continue
        a, alen = anchor(code, mask)
        if alen < 5:
            continue
        tried += 1
        needle = code[a:a + alen]
        hits = []
        for sname, va, data in sections:
            at = data.find(needle)
            while at != -1 and len(hits) < 3:
                s = at - a
                if s >= 0 and s + len(code) <= len(data):
                    seg = data[s:s + len(code)]
                    if all(not m or x == y for x, y, m in zip(seg, code, mask)):
                        hits.append(va + s)
                at = data.find(needle, at + 1)
        if len(hits) == 1:
            name = sorted(names, key=lambda x: (x.startswith('_') and '@' not in x and '?' not in x, len(x)))[0]
            prev = labels.get(hits[0])
            if prev is None or len(code) > prev[1]:
                labels[hits[0]] = (name, len(code))
        elif hits:
            ambiguous += 1
    json.dump({f'{a:#010x}': n for a, (n, _) in sorted(labels.items())}, open(out, 'w'), indent=0)
    print(f'{len(funcs)} library functions, {tried} searchable, {len(labels)} placed, {ambiguous} ambiguous',
          file=sys.stderr)


if __name__ == '__main__':
    main()
