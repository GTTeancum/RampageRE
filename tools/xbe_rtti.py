"""Class names and virtual tables of an MSVC-built Xbox executable, from its RTTI.

.data holds a type descriptor per class (".?AVName@@" eight bytes in); .rdata holds, before each virtual table, a
pointer to a complete-object locator {0, offset, cdOffset, type descriptor, class hierarchy}. The hierarchy gives
the base classes.

Usage: python tools/xbe_rtti.py XBE OUT.json
OUT.json: {"classes": {name: {"bases": [...], "vtables": [{"address", "offset", "functions": [...]}]}},
           "functions": {address: "Class::vf3"}}   (a function shared with a base is named for the base-most class)
"""
import json
import re
import struct
import sys


def main():
    d = open(sys.argv[1], 'rb').read()
    base = struct.unpack_from('<I', d, 0x104)[0]
    nsec, secaddr = struct.unpack_from('<II', d, 0x11C)
    secs = []
    for i in range(nsec):
        fl, va, vsz, raw, rsz, naddr = struct.unpack_from('<IIIIII', d, secaddr - base + i * 56)
        name = d[naddr - base:naddr - base + 16].split(b'\0')[0].decode('latin1')
        secs.append((name, va, vsz, raw, rsz, fl))

    def off(va):
        for name, sva, vsz, raw, rsz, fl in secs:
            if sva <= va < sva + rsz:
                return raw + va - sva
        return None

    def u32(va):
        o = off(va)
        return struct.unpack_from('<I', d, o)[0] if o is not None and o + 4 <= len(d) else None

    # (the executable flag is not reliable: .rdata carries it in some builds) code is what lies below .rdata
    rdata = next((sva for name, sva, vsz, raw, rsz, fl in secs if name == '.rdata'), 0xFFFFFFFF)

    def is_code(va):
        return any(sva < rdata and sva <= va < sva + vsz for name, sva, vsz, raw, rsz, fl in secs)

    def demangle(s):                        # .?AVname@ns@@ -> ns::name
        m = re.match(r'\.\?A[VU](.+)@@$', s)
        return '::'.join(reversed(m.group(1).split('@'))) if m else s

    tds = {}
    for name, sva, vsz, raw, rsz, fl in secs:
        blob = d[raw:raw + rsz]
        for m in re.finditer(rb'\.\?A[VU][\x21-\x7e]+?@@\x00', blob):
            tds[sva + m.start() - 8] = demangle(m.group()[:-1].decode('latin1'))
    classes = {n: {'bases': [], 'vtables': []} for n in tds.values()}
    for name, sva, vsz, raw, rsz, fl in secs:
        if name not in ('.rdata', '.data'):
            continue
        for o in range(0, rsz - 20, 4):
            sig, offset, cd, td, chd = struct.unpack_from('<5I', d, raw + o)
            if sig != 0 or td not in tds or offset > 0x10000:
                continue
            col = sva + o
            cls = tds[td]
            if not classes[cls]['bases'] and u32(chd) is not None:      # class hierarchy: sig, attrs, n, array
                n, arr = u32(chd + 8), u32(chd + 12)
                if n and n < 64 and arr:
                    for k in range(1, n):
                        bcd = u32(arr + 4 * k)
                        btd = u32(bcd) if bcd else None
                        if btd in tds:
                            classes[cls]['bases'].append(tds[btd])
            classes[cls].setdefault('_cols', []).append((col, offset))
    cols = {col: (cls, offset) for cls, c in classes.items() for col, offset in c.get('_cols', [])}
    for name, sva, vsz, raw, rsz, fl in secs:
        if name not in ('.rdata', '.data'):
            continue
        for o in range(0, rsz - 8, 4):
            p = struct.unpack_from('<I', d, raw + o)[0]
            if p not in cols:
                continue
            cls, offset = cols[p]
            vt = sva + o + 4
            fns = []
            while True:
                f = u32(vt + 4 * len(fns))
                if f is None or not is_code(f) or (len(fns) and (vt + 4 * len(fns) - 4) in ()):
                    break
                if len(fns) and struct.unpack_from('<I', d, off(vt + 4 * len(fns)))[0] in cols:
                    break
                fns.append(f)
                if len(fns) > 400:
                    break
            if fns:
                classes[cls]['vtables'].append({'address': f'{vt:#010x}', 'offset': offset,
                                                'functions': [f'{f:#010x}' for f in fns]})
    for c in classes.values():
        c.pop('_cols', None)
    # name each virtual function for the base-most class whose primary table has it at that slot
    depth = {n: len(c['bases']) for n, c in classes.items()}
    fn = {}
    for cls in sorted(classes, key=lambda n: depth[n]):
        for vt in classes[cls]['vtables']:
            for k, f in enumerate(vt['functions']):
                fn.setdefault(f, f'{cls}::vf{k}' + (f'_at{vt["offset"]}' if vt['offset'] else ''))
    json.dump({'classes': classes, 'functions': fn}, open(sys.argv[2], 'w'), indent=1)
    withvt = sum(1 for c in classes.values() if c['vtables'])
    print(f'{len(classes)} classes, {withvt} with virtual tables, {len(fn)} virtual functions')


if __name__ == '__main__':
    main()
