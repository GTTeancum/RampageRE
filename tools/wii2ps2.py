"""Convert the Wii disc's extra content into the PS2's formats, for the Xbox preparation tool (tools/xbox_prep).

The port plays the Wii's ten monsters and Dallas from the player's own Wii disc beside the PS2 data
(docs/WII_PLAN.md). A console has no time to convert formats while it plays, so the conversion is done once on the
PC, before the first launch, and the game reads PS2-format files only.

  python tools/wii2ps2.py sound WII_AUDIO_DIR PS2_UBER_DIR OUT_DIR [--verify]
  python tools/wii2ps2.py models WII_BUNDLE OUT_BUNDLE

sound: for each bank (Rampage, RShell) of WII_AUDIO_DIR (the Wii's Audio folder: NAME.uber, big endian "REBU",
       and NAME.samp, DSP-ADPCM) that has events the PS2's bank of the same name (PS2_UBER_DIR/NAME.uber, out of
       RAMPAGE.VOL) has not got, writes OUT_DIR/NAME.uber and NAME.samp in the PS2's layout (docs/FORMAT_SOUND.md):
       the same events, programs and names, little endian, 16-byte sample entries, and PS-ADPCM samples.
       Only the samples those extra events can play are stored (their own, and those of the events their programs
       go to): the game asks the PS2's bank first, so a Wii copy of a PS2 sound is never played; the other entries
       are left absent (offset 0xFFFFFFFF, as entry 0 is). Each sound is decoded and encoded again once: the two
       consoles' ADPCM are different codes. --verify decodes what was written and reports how close it is.

models: a Wii bundle (.bdg) with its rigid meshes (entry type 0) rewritten in the PS2's mesh format
       (docs/FORMAT_BDP.md "Meshes"): the part records little endian, each part one section of vertices in
       strips (position 3 floats and the closes-a-triangle flag; normals 3 signed bytes; colours 4 bytes, 0x80 =
       1; texture coordinates 2 x 4.12 fixed point). The Wii keeps them as GX display lists over interleaved
       float vertices, which the game has to parse differently and more slowly. Everything else in the bundle
       is byte for byte the Wii's (its tables, textures, animations: the game converts their byte order when it
       loads the bundle, never while it plays), and the byte at 0x2B of the header is set to 'P' so the game
       knows which reader a rigid mesh wants. Skinned meshes (type 17) are left: the PS2's holds two bones a
       vertex and the Wii's monsters use up to four.
       What the PS2's format holds less finely is rounded to it: a normal to 1/127, a colour to 1/128, a texture
       coordinate to 1/4096 (and moved by whole repeats into its range of 8); positions and triangles are
       exact. The result is read back with the PS2's reader and every triangle compared with the Wii's.
"""
import math
import os
import struct
import sys

sys.path.append(os.path.dirname(os.path.abspath(__file__)))   # (after the library's: tools has a dis.py)
import sound                                             # the banks' reader and the two decoders

FILTERS = sound.FILTERS                                  # PS-ADPCM's prediction filters, in 1/64


def needed_samples(wii, ps2):
    """The SDIR indices the Wii bank's own events can play: the events whose names the PS2's bank has not got, and
    every event their programs go to or start (06, 08, 13), their `play sample` instructions (10)."""
    have = {n.lower() for n in ps2.names} if ps2 else set()
    todo = [i for i, n in enumerate(wii.names) if n.lower() not in have]
    extra, reached, samples = len(todo), set(todo), set()
    while todo:
        ev = todo.pop()
        pi = wii.proj[ev][2]
        prog = wii.progs[pi] if pi < len(wii.progs) else b''
        for o in range(0, len(prog) - 7, 8):
            ins = prog[o:o + 8]
            if ins[0] == 0x10:
                samples.add(ins[1] << 8 | ins[2])
            elif ins[0] in (0x06, 0x08, 0x13):
                t = ins[2] << 8 | ins[3]
                if t < len(wii.names) and t not in reached:
                    reached.add(t)
                    todo.append(t)
    return extra, {s for s in samples if 0 < s < len(wii.sdir)}


def adpcm_encode(pcm):
    """16-bit samples to PS-ADPCM frames (16 bytes for 28 samples; the last frame is filled with silence and
    carries the end flag, as the PS2's one-shot samples do). For each frame every filter is tried with the shift
    that fits its largest prediction error and the one above it, encoded as the decoder will hear it (each
    sample's error fed into the next), and the closest kept."""
    n = len(pcm)
    frames = max(1, (n + 27) // 28)
    pcm = list(pcm) + [0] * (frames * 28 - n)
    out = bytearray()
    h1 = h2 = 0
    for fr in range(frames):
        s = pcm[fr * 28:fr * 28 + 28]
        best = None
        for filt, (f0, f1) in enumerate(FILTERS):
            # the largest error predicting from the true samples before: the shift that holds it in a nibble
            a1, a2, peak = h1, h2, 0
            for x in s:
                e = x - ((a1 * f0 + a2 * f1 + 32) >> 6)
                if e < 0:
                    e = -e
                if e > peak:
                    peak = e
                a2, a1 = a1, x
            shift = 12
            while shift > 0 and (peak >> (12 - shift)) > 7:
                shift -= 1
            for sh in (shift, shift - 1) if shift > 0 else (shift,):
                step = 12 - sh
                half = (1 << step) >> 1
                d1, d2, err, nib = h1, h2, 0, []
                for x in s:
                    p = (d1 * f0 + d2 * f1 + 32) >> 6
                    q = (x - p + half) >> step
                    if q > 7:
                        q = 7
                    elif q < -8:
                        q = -8
                    v = p + (q << step)
                    if v > 32767:
                        v = 32767
                    elif v < -32768:
                        v = -32768
                    err += (x - v) * (x - v)
                    nib.append(q & 15)
                    d2, d1 = d1, v
                if best is None or err < best[0]:
                    best = (err, filt, sh, nib, d1, d2)
        _, filt, sh, nib, h1, h2 = best
        out.append(sh | filt << 4)
        out.append(1 if fr == frames - 1 else 0)
        for k in range(0, 28, 2):
            out.append(nib[k] | nib[k + 1] << 4)
    return bytes(out)


def convert_bank(wii_dir, ps2_dir, out_dir, name, verify=False, log=print):
    wii_path = os.path.join(wii_dir, name + '.uber')
    if not os.path.exists(wii_path):
        return None
    wii = sound.Bank(wii_path)
    if not wii.wii:
        raise ValueError(f'{wii_path} is not a Wii bank (REBU)')
    ps2_path = os.path.join(ps2_dir, name + '.uber')
    ps2 = sound.Bank(ps2_path) if os.path.exists(ps2_path) else None
    extra, need = needed_samples(wii, ps2)
    if not extra:
        log(f'{name}: every event is in the PS2 bank; nothing to convert')
        return None
    d = bytearray(open(wii_path, 'rb').read())
    proj, pool, sdir, gcount, gnames, names = struct.unpack_from('>6I', d, 8)
    nproj = struct.unpack_from('>I', d, proj + 8)[0]
    npool = struct.unpack_from('>I', d, pool + 8)[0]
    nsdir = struct.unpack_from('>I', d, sdir + 12)[0]
    ngroups = struct.unpack_from('>H', d, gnames)[0]
    nnames = struct.unpack_from('>H', d, names)[0]
    if not (proj < pool < sdir < gcount < gnames < names) or sdir + 16 + 64 * nsdir != gcount:
        raise ValueError(f'{wii_path}: sections not in the expected order')
    shrink = (64 - 16) * nsdir                           # the directory's entries are 16 bytes on the PS2

    def swap(at, fmt):                                   # big endian fields at `at` rewritten little endian
        vals = struct.unpack_from('>' + fmt, d, at)
        struct.pack_into('<' + fmt, d, at, *vals)
        return vals

    # the samples: decoded from the Wii's DSP-ADPCM, encoded as the PS2's
    samp = bytearray()
    entries = []
    stats = []
    for i in range(nsdir):
        start, count, flags, rate = wii.sdir[i]
        if i not in need or start == 0xFFFFFFFF:
            entries.append((0xFFFFFFFF, 0xFFFFFFFF, flags if start != 0xFFFFFFFF else 0xFFFFFFFF,
                            rate if start != 0xFFFFFFFF else 0xFFFFFFFF))
            continue
        if flags & 1:
            raise ValueError(f'{name}: sample {i} loops; a looping sample needs its length made a whole number '
                             'of frames, which this converter does not do yet')
        pcm, _ = wii.decode(i)
        enc = adpcm_encode(pcm)
        entries.append((len(samp), len(enc), flags, rate))
        if verify:
            back, _, _ = sound.adpcm_decode(enc)
            sig = sum(x * x for x in pcm) or 1
            noise = sum((a - b) * (a - b) for a, b in zip(pcm, back)) or 1
            import math
            stats.append((10 * math.log10(sig / noise), i, len(pcm), rate, len(back) - len(pcm)))
        samp += enc
        if len(entries) % 1 == 0 and verify and len(stats) % 20 == 0:
            log(f'  {len(stats)} of {len(need)} samples')

    swap(8, '6I')
    swap(proj + 8, 'I')
    for i in range(nproj):
        swap(proj + 16 + 12 * i, 'fIH')                  # (then two bytes: group, max)
    swap(pool + 8, 'I')
    swap(pool + 16, f'{npool}i')                         # (the programs' own operands are big endian on both)
    head = bytes(d[:sdir])
    new_sdir = b'SDIR' + struct.pack('<HHII', 1, 1, len(samp), nsdir) + b''.join(struct.pack('<4I', *e) for e in entries)
    tail = bytearray(d[gcount:])
    base = gcount                                        # what `tail` starts at in the Wii file

    def tswap(at, fmt, move=False):                      # in the tail; offsets into the file move down
        vals = struct.unpack_from('>' + fmt, tail, at - base)
        if move:
            vals = tuple(v - shrink if v >= gcount else v for v in vals)
        struct.pack_into('<' + fmt, tail, at - base, *vals)

    tswap(gcount, f'{ngroups}H')
    tswap(gnames, 'H')
    tswap(gnames + 4, f'{ngroups}I', move=True)
    tswap(names, 'H')
    tswap(names + 4, f'{nnames}I', move=True)
    out = bytearray(head + new_sdir + bytes(tail))
    out[0:8] = b'UBER' + struct.pack('<HH', 1, 1)
    out[proj:proj + 8] = b'PROJ' + struct.pack('<HH', 2, 1)
    out[pool:pool + 8] = b'POOL' + struct.pack('<HH', 1, 1)
    struct.pack_into('<6I', out, 8, proj, pool, sdir, gcount - shrink, gnames - shrink, names - shrink)

    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, name + '.uber'), 'wb') as f:
        f.write(out)
    with open(os.path.join(out_dir, name + '.samp'), 'wb') as f:
        f.write(samp)
    # the result read back as a PS2 bank: the same events, programs and names
    back = sound.Bank(os.path.join(out_dir, name + '.uber'))
    assert not back.wii and back.names == wii.names and back.groups == wii.groups, 'names'
    assert list(back.group_counts) == list(wii.group_counts), 'group counts'
    assert back.proj == wii.proj and back.progs == wii.progs, 'events or programs'
    assert len(back.sdir) == len(wii.sdir), 'directory'
    log(f'{name}: {extra} events the PS2 bank has not got, {len(need)} samples, '
        f'{len(samp)} bytes of PS-ADPCM (the Wii bank: {os.path.getsize(os.path.join(wii_dir, name + ".samp"))})')
    if verify and stats:
        stats.sort()
        log(f'  signal to noise after the second coding: lowest {stats[0][0]:.1f} dB (sample {stats[0][1]}), '
            f'median {stats[len(stats) // 2][0]:.1f} dB, highest {stats[-1][0]:.1f} dB')
    return len(need), len(samp)


# ---------------------------------------------------------------- rigid meshes: GX display lists to PS2 strips

def strips_of(tris):
    """The triangles (index triples, wound as given) as strips: a list of vertex index lists. In a strip of
    v0 v1 v2 v3 ... triangle k is (v[k], v[k+1], v[k+2]), every other one turned. Each strip is grown from an
    unused triangle, trying its three corners as the start and keeping the longest."""
    edge = {}                                            # directed edge -> the triangles that have it
    for t, (a, b, c) in enumerate(tris):
        for e in ((a, b), (b, c), (c, a)):
            edge.setdefault(e, []).append(t)
    used = [False] * len(tris)

    def third(t, u, v):                                  # the corner of t that is not on its edge u -> v
        a, b, c = tris[t]
        return c if (a, b) == (u, v) else a if (b, c) == (u, v) else b

    def grow(t, rot, commit):
        a, b, c = tris[t][rot:] + tris[t][:rot]
        seq, taken = [a, b, c], [t]
        mine = {t}
        while True:
            k = len(seq) - 2                             # the next triangle's place in the strip
            u, v = (seq[-1], seq[-2]) if k % 2 else (seq[-2], seq[-1])
            nxt = next((x for x in edge.get((u, v), ()) if not used[x] and x not in mine), None)
            if nxt is None:
                break
            seq.append(third(nxt, u, v))
            taken.append(nxt)
            mine.add(nxt)
        if commit:
            for x in taken:
                used[x] = True
        return seq

    out = []
    for t in range(len(tris)):
        if used[t]:
            continue
        if len(set(tris[t])) < 3:                        # (a degenerate triangle draws nothing: as its own strip)
            used[t] = True
            out.append(list(tris[t]))
            continue
        best = max(range(3), key=lambda r: len(grow(t, r, False)))
        out.append(grow(t, best, True))
    return out


def ps2_section(sec, log):
    """A Wii section (tools/mesh.py Section: float vertices, a triangle list) as a PS2 section: (vertex count,
    format, the stream bytes), the vertices in strips. A strip must start on an even vertex for its first
    triangle to wind as given (mesh.c read_section turns the triangle closed by an odd vertex): one vertex is
    repeated before a strip that would start on an odd one."""
    NORMAL, COLOUR, UV = 0x010, 0x040, 0x100
    fmt = sec.format & (0x002 | NORMAL | COLOUR | UV)
    if sec.format & ~(0x002 | NORMAL | COLOUR | UV):
        raise ValueError(f'vertex format {sec.format:#x} has parts the PS2 format has not got')
    order, closes, shift = [], [], []                    # vertex index, closes a triangle, whole repeats taken off uv
    for strip in strips_of(sec.tris):
        pieces = [strip]
        if sec.uvs:                                      # the 4.12 coordinates hold (-8, 8): whole repeats come off
            def room(vs):
                us = [sec.uvs[v] for v in vs]
                du, dv = (math.floor((min(u[k] for u in us) + max(u[k] for u in us)) / 2 + 0.5) for k in (0, 1))
                ok = all(abs(u[0] - du) < 7.999 and abs(u[1] - dv) < 7.999 for u in us)
                return ok, (du, dv)
            ok, d = room(strip)
            if ok and all(abs(c) < 7.999 for u in (sec.uvs[v] for v in strip) for c in u):
                d = (0, 0)                               # (in range as it is: untouched)
            if not ok:                                   # too wide for one strip: its triangles one by one
                pieces = []
                for k in range(len(strip) - 2):
                    tri = strip[k:k + 3] if k % 2 == 0 else [strip[k + 1], strip[k], strip[k + 2]]
                    pieces.append(tri)
            shifts = []
            for piece in pieces:
                ok, d2 = room(piece)
                if not ok:
                    raise ValueError('a triangle spans more than 8 repeats of its texture: the PS2 format cannot hold it')
                if all(abs(c) < 7.999 for u in (sec.uvs[v] for v in piece) for c in u):
                    d2 = (0, 0)
                shifts.append(d2)
        else:
            shifts = [(0, 0)]
        for piece, d in zip(pieces, shifts):
            if len(order) % 2:                           # an odd start: the first vertex once more, closing nothing
                order.append(piece[0]); closes.append(0); shift.append(d)
            for k, v in enumerate(piece):
                order.append(v); closes.append(1 if k >= 2 else 0); shift.append(d)
    n = len(order)
    def pad(b):
        return b + bytes(-len(b) % 16)
    out = pad(b''.join(struct.pack('<4f', *sec.positions[v], float(c)) for v, c in zip(order, closes)))
    if fmt & NORMAL:
        def s8(x):
            return max(-127, min(127, int(round(x * 127))))
        out += pad(b''.join(struct.pack('<3bB', *(s8(c) for c in sec.normals[v]), 0) for v in order))
    if fmt & COLOUR:
        out += pad(b''.join(bytes(int(round(round(c * 255) * 128 / 255)) for c in sec.colours[v]) for v in order))
    if fmt & UV:
        def fx(x):
            q = int(round(x * 4096))
            if not -32768 <= q <= 32767:
                raise ValueError('texture coordinate out of the 4.12 range')
            return q
        out += pad(b''.join(struct.pack('<2h', fx(sec.uvs[v][0] - d[0]), fx(sec.uvs[v][1] - d[1]))
                            for v, d in zip(order, shift)))
    return n, fmt, out, sum(1 for d in shift if d != (0, 0))


def ps2_mesh_entry(data, data2, log):
    """A Wii type 0 entry as a PS2 one: (data, data2, vertices before, vertices after, triangles)."""
    import mesh
    parts = mesh.read_wii_mesh(0, data, data2)
    count = struct.unpack_from('>I', data, 0)[0]
    if count != len(parts):
        raise ValueError('part count')
    head = bytearray(struct.pack('<I7f', *struct.unpack_from('>I7f', data, 0)))
    recs, tail, payload = bytearray(), bytearray(), bytearray()
    tail_at = 0x20 + 0x50 * count
    before = after = ntris = moved = 0
    for i, part in enumerate(parts):
        sec = part.sections[0]
        n, fmt, streams, mv = ps2_section(sec, log)
        moved += mv
        before += sec.count; after += n; ntris += len(sec.tris)
        r = bytearray(data[0x20 + 0x50 * i:0x20 + 0x50 * (i + 1)])
        words = list(struct.unpack('>20I', r))
        rec = bytearray(struct.pack('<20I', *words))
        rec[8:12] = struct.pack('<2H', *struct.unpack('>2H', r[8:12]))     # (+0x0A the material's name index)
        struct.pack_into('<I', rec, 0, n - 2)                             # the strip's length - 2
        here = 0x20 + 0x50 * i
        block_at = tail_at + len(tail)
        tail += bytes(20)                                                  # (the section block: nothing the port reads)
        desc_at = tail_at + len(tail)
        at2 = len(payload)
        tail += struct.pack('<6I', n, 0, fmt, 0, at2, len(streams))
        struct.pack_into('<I', rec, 0x3C, block_at - here)
        struct.pack_into('<I', rec, 0x40, desc_at - here)
        recs += rec
        payload += streams
    return bytes(head + recs + tail), bytes(payload), before, after, ntris, moved


def corner_set(parts, quant, wii):
    """The triangles of a mesh as a sorted list of corner triples, each corner what the PS2's format holds of it."""
    import mesh
    out = []
    for part in parts:
        sec = part.sections[0]
        tris = mesh.triangles(sec)
        def corner(v):
            c = [tuple(struct.unpack('<3f', struct.pack('<3f', *sec.positions[v])))]
            if sec.normals:
                c.append(tuple(max(-127, min(127, int(round(x * 127)))) for x in sec.normals[v]))
            if sec.colours:
                c.append(tuple(int(round(round(x * 255) * 128 / 255)) if wii else min(128, int(round(x * 128)))
                               for x in sec.colours[v]))
            if sec.uvs:                                  # (whole repeats are the same texels)
                c.append(tuple(int(round(x * 4096)) % 4096 for x in sec.uvs[v]))
            return tuple(c)
        for a, b, cc in tris:
            if len({a, b, cc}) < 3:
                continue
            k = [corner(a), corner(b), corner(cc)]
            if len(set(k)) < 3:                          # (two corners at the same vertex: draws nothing)
                continue
            m = min(range(3), key=lambda j: k[j])
            out.append((part.material, tuple(k[m:] + k[:m])))
    return sorted(out)


def ps2_triangles(sec):
    """The triangles of a PS2 section as the game makes them (mesh.c read_section): a vertex that closes one makes
    it with the two before it, turned when its own index is odd."""
    out = []
    for i, closes in enumerate(sec.closes):
        if closes and i >= 2:
            out.append((i - 1, i - 2, i) if i & 1 else (i - 2, i - 1, i))
    return out


def rebuild(b, replacements):
    """The bundle laid out again with some entries' (data, data2) replaced: the header, the name table, then for
    each segment its header, entry table, the entries' data and their data2, each on a 32-byte boundary (as
    tools/xbox_assets.py rebuild_bundle, which needs Pillow for its other work). Everything not replaced is byte for
    byte what it was; checked by reading the result back."""
    import bdp

    def align(out, n=32):
        out.extend(bytes(-len(out) % n))
        return len(out)

    e = b.endian
    out = bytearray(b.data[:0x60])
    names_end = max([4 + 4 * len(b.names)] + [o + len(n.encode('latin1')) + 1 for o, n in zip(b.name_offsets, b.names)])
    names_at = align(out)
    out.extend(b.data[b.names_at:b.names_at + names_end])
    struct.pack_into(e + 'I', out, 0x34, names_at)
    for si, seg in enumerate(b.segments):
        header = align(out)
        out.extend(bytes(24 + 18 * len(seg.records)))
        struct.pack_into(e + 'I', out, 0x38 + 4 * si, header)
        entries_at = align(out)
        out.extend(b.data[seg.entries_offset:seg.entries_offset + 16 * len(seg.entries)])
        items = [replacements.get((si, j), (a, d2)) for j, (_, a, d2) in enumerate(b.items(si))]
        places, places2 = [], []
        data_at = align(out)
        for a, _ in items:
            places.append((align(out) - data_at, len(a)))
            out.extend(a)
        data_size = len(out) - data_at
        data2_at = align(out)
        for _, d2 in items:
            places2.append((align(out) - data2_at, len(d2)))
            out.extend(d2)
        data2_size = len(out) - data2_at
        struct.pack_into(e + 'HHIIIII', out, header, seg.number, len(seg.records), entries_at, data_at, data_size,
                         data2_at, data2_size)
        for j, (r, (a, n), (d2, n2)) in enumerate(zip(seg.records, places, places2)):
            struct.pack_into(e + 'HIIII', out, header + 24 + 18 * j, r.entry, a, n, d2, n2)
    align(out)
    after = bdp.Bundle(bytes(out))
    if after.names != b.names or after.absent != b.absent:
        raise ValueError('the name table or the absent segments changed')
    for si in range(10):
        old, new = list(b.items(si)), list(after.items(si))
        if len(old) != len(new):
            raise ValueError('entry count changed')
        for j, ((oe, oa, od), (ne, na, nd)) in enumerate(zip(old, new)):
            if oe != ne or (na, nd) != replacements.get((si, j), (oa, od)):
                raise ValueError(f'segment {si} entry {j} is not what was written')
    return bytes(out)


def convert_models(src, dst, log=print):
    import bdp, mesh
    raw = open(src, 'rb').read()
    b = bdp.Bundle(raw)
    if b.endian != '>':
        raise ValueError(f'{src} is not a Wii bundle')
    if raw[0x2B] == ord('P'):
        raise ValueError(f'{src} is already converted')
    replacements, nmesh, before, after, ntris, moved = {}, 0, 0, 0, 0, 0
    for si in range(10):
        for j, (e, data, data2) in enumerate(b.items(si)):
            if e.type != 0 or not data:
                continue
            nd, nd2, v0, v1, t, mv = ps2_mesh_entry(data, data2, log)
            # read back with the PS2's reader: the same triangles, corner for corner
            back = mesh.read_mesh(0, nd, nd2)
            for part in back:
                part.sections[0] = part.sections[0]._replace(tris=ps2_triangles(part.sections[0]))
            if corner_set(mesh.read_wii_mesh(0, data, data2), None, True) != corner_set(back, None, False):
                raise ValueError(f'{b.name(e.name)}: the converted mesh is not the same triangles')
            replacements[(si, j)] = (nd, nd2)
            nmesh += 1; before += v0; after += v1; ntris += t; moved += mv
    if not replacements:
        log(f'{os.path.basename(src)}: no rigid meshes')
        return None
    out = bytearray(rebuild(b, replacements))
    out[0x2B] = ord('P')                                 # (the magic's zero padding: rigid meshes are the PS2's)
    with open(dst, 'wb') as f:
        f.write(out)
    log(f'{os.path.basename(src)}: {nmesh} rigid meshes, {ntris} triangles; vertices {before} in the Wii\'s lists, '
        f'{after} in strips ({after / max(1, ntris):.2f} a triangle); {moved} vertices moved by whole repeats; '
        f'{len(raw)} -> {len(out)} bytes')
    return nmesh, before, after, ntris


def main():
    a = sys.argv[1:]
    if len(a) >= 3 and a[0] == 'models':
        return 0 if convert_models(a[1], a[2]) else 1
    if len(a) >= 4 and a[0] == 'sound':
        verify = '--verify' in a
        done = 0
        for name in ('Rampage', 'RShell'):
            if convert_bank(a[1], a[2], a[3], name, verify):
                done += 1
        return 0 if done else 1
    sys.exit(__doc__)


if __name__ == '__main__':
    sys.exit(main())
