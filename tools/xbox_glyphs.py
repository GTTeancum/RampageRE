#!/usr/bin/env python3
"""Make XBOXTEX.RXT, the Xbox build's replacement pictures (src/texpack_xbox.c): the Xbox button art in place of
the PS2's.

The game's button pictures are textures of CommonGUI.bdp named Controller_B0, Controller_B1, Controller_Back,
Controller_Select, Controller_White, Trigger_Left and Trigger_Right (32 x 32, the triggers 32 x 16). The Xbox build
shows a picture of XBOXTEX.RXT in place of any bundle texture of the same name, so the file needs one picture per
name. Put it beside RAMPAGE.VOL on the console.

No art is part of this repository. The pictures come from your own files:

  xbox_glyphs.py prototype <folder of the Xbox prototype> XBOXTEX.RXT
      the studio's own Xbox pictures, read from the prototype's Shell/CommonGUI.bdl (same names, same sizes)

  xbox_glyphs.py png <folder of PNGs> XBOXTEX.RXT
      any pictures of your own: one <TextureName>.png per texture to replace (RGBA; keep the original's shape)

  xbox_glyphs.py list XBOXTEX.RXT
  xbox_glyphs.py dump XBOXTEX.RXT <folder>            (writes each picture back out as a PNG)

File: "RXTP", u32 version 1, u32 count, count x { char name[40], u32 offset, u16 width, u16 height }, then the
pictures' RGBA bytes, top row first."""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

BUTTONS = ('Controller_B0', 'Controller_B1', 'Controller_Back', 'Controller_Select', 'Controller_White',
           'Trigger_Left', 'Trigger_Right')
MAX = 64


def c565(v):
    return ((v >> 11 & 31) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31)


def dxt_colours(data, o, opaque):
    a, b, bits = struct.unpack_from('<HHI', data, o)
    ca, cb = c565(a), c565(b)
    if a > b or opaque:
        pal = [ca, cb, tuple((2 * x + y) // 3 for x, y in zip(ca, cb)), tuple((x + 2 * y) // 3 for x, y in zip(ca, cb))]
        al = [255] * 4
    else:
        pal = [ca, cb, tuple((x + y) // 2 for x, y in zip(ca, cb)), (0, 0, 0)]
        al = [255, 255, 255, 0]
    return pal, al, bits


def decode_dxt(data, w, h, kind):
    """DXT1 / DXT3 / DXT5 (kind 1, 3, 5) to RGBA bytes, top row first"""
    out = bytearray(w * h * 4)
    o = 0
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            alpha = None
            if kind == 3:
                bits64 = struct.unpack_from('<Q', data, o)[0]
                alpha = [((bits64 >> (4 * i)) & 15) * 17 for i in range(16)]
                o += 8
            elif kind == 5:
                a0, a1 = data[o], data[o + 1]
                bits48 = int.from_bytes(data[o + 2:o + 8], 'little')
                if a0 > a1:
                    tab = [a0, a1] + [((7 - k) * a0 + k * a1) // 7 for k in range(1, 7)]
                else:
                    tab = [a0, a1] + [((5 - k) * a0 + k * a1) // 5 for k in range(1, 5)] + [0, 255]
                alpha = [tab[(bits48 >> (3 * i)) & 7] for i in range(16)]
                o += 8
            pal, al, bits = dxt_colours(data, o, kind != 1)
            o += 8
            for i in range(16):
                x, y = bx + i % 4, by + i // 4
                if x >= w or y >= h:
                    continue
                k = (bits >> (2 * i)) & 3
                p = 4 * (y * w + x)
                out[p:p + 3] = bytes(pal[k])
                out[p + 3] = alpha[i] if alpha is not None else al[k]
    return bytes(out)


def unswizzle(data, w, h, bpp):
    """the Xbox's swizzled (Morton order) pixels to rows"""
    out = bytearray(w * h * bpp)
    for y in range(h):
        for x in range(w):
            i = 0
            bit = 0
            xs, ys = x, y
            mw, mh = w, h
            while mw > 1 or mh > 1:
                if mw > 1:
                    i |= (xs & 1) << bit
                    xs >>= 1
                    bit += 1
                    mw >>= 1
                if mh > 1:
                    i |= (ys & 1) << bit
                    ys >>= 1
                    bit += 1
                    mh >>= 1
            out[(y * w + x) * bpp:(y * w + x + 1) * bpp] = data[i * bpp:(i + 1) * bpp]
    return bytes(out)


def from_prototype(folder):
    import bdp
    path = None
    for cand in (os.path.join(folder, 'Shell', 'CommonGUI.bdl'), os.path.join(folder, 'CommonGUI.bdl')):
        if os.path.exists(cand):
            path = cand
    if not path:
        sys.exit('no Shell/CommonGUI.bdl under ' + folder)
    bundle = bdp.Bundle(open(path, 'rb').read())
    pictures = []
    for seg in bundle.segments:
        if not seg:
            continue
        for r in seg.records:
            if r.entry >= len(seg.entries) or seg.entries[r.entry].type != 9:
                continue
            name = bundle.names[seg.entries[r.entry].name].lstrip('\x13')
            if name not in BUTTONS:
                continue
            head = seg.data_offset + r.data_offset
            fmt = struct.unpack_from('<I', bundle.data, head + 0xC)[0]     # the D3D texture format word
            kind = (fmt >> 8) & 0xFF
            w, h = 1 << ((fmt >> 20) & 15), 1 << ((fmt >> 24) & 15)
            px = bundle.data[seg.data2_offset + r.data2_offset:seg.data2_offset + r.data2_offset + r.data2_size]
            if kind in (0x0C, 0x0E, 0x0F):
                rgba = decode_dxt(px, w, h, {0x0C: 1, 0x0E: 3, 0x0F: 5}[kind])
            elif kind == 0x06:                                           # A8R8G8B8, swizzled
                bgra = unswizzle(px, w, h, 4)
                rgba = b''.join(bgra[i + 2:i + 3] + bgra[i + 1:i + 2] + bgra[i:i + 1] + bgra[i + 3:i + 4]
                                for i in range(0, len(bgra), 4))
            else:
                print('%s: texture format %#x is not handled, left out' % (name, kind))
                continue
            pictures.append((name, w, h, rgba))
    return pictures


def from_png(folder):
    from PIL import Image
    pictures = []
    for f in sorted(os.listdir(folder)):
        if not f.lower().endswith('.png'):
            continue
        im = Image.open(os.path.join(folder, f)).convert('RGBA')
        pictures.append((os.path.splitext(f)[0], im.width, im.height, im.tobytes()))
    return pictures


def write(path, pictures):
    if not pictures:
        sys.exit('no pictures found')
    if len(pictures) > MAX:
        sys.exit('at most %d pictures' % MAX)
    table = b''
    body = b''
    at = 12 + 48 * len(pictures)
    for name, w, h, rgba in pictures:
        raw = name.encode('ascii')
        if len(raw) > 39 or len(rgba) != w * h * 4 or w > 1024 or h > 1024:
            sys.exit('%s: name too long or a bad picture' % name)
        table += raw.ljust(40, b'\0') + struct.pack('<IHH', at + len(body), w, h)
        body += rgba
    with open(path, 'wb') as f:
        f.write(b'RXTP' + struct.pack('<II', 1, len(pictures)) + table + body)
    for name, w, h, _ in pictures:
        print('%-22s %d x %d' % (name, w, h))
    print('%d pictures -> %s' % (len(pictures), path))


def read(path):
    d = open(path, 'rb').read()
    if d[:4] != b'RXTP' or struct.unpack_from('<I', d, 4)[0] != 1:
        sys.exit('not an XBOXTEX.RXT')
    out = []
    for i in range(struct.unpack_from('<I', d, 8)[0]):
        rec = d[12 + 48 * i:60 + 48 * i]
        at, w, h = struct.unpack_from('<IHH', rec, 40)
        out.append((rec[:40].split(b'\0')[0].decode('ascii'), w, h, d[at:at + w * h * 4]))
    return out


def main(argv):
    if len(argv) == 4 and argv[1] == 'prototype':
        write(argv[3], from_prototype(argv[2]))
    elif len(argv) == 4 and argv[1] == 'png':
        write(argv[3], from_png(argv[2]))
    elif len(argv) == 3 and argv[1] == 'list':
        for name, w, h, _ in read(argv[2]):
            print('%-22s %d x %d' % (name, w, h))
    elif len(argv) == 4 and argv[1] == 'dump':
        from PIL import Image
        os.makedirs(argv[3], exist_ok=True)
        for name, w, h, rgba in read(argv[2]):
            Image.frombytes('RGBA', (w, h), rgba).save(os.path.join(argv[3], name + '.png'))
            print(name)
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main(sys.argv)
