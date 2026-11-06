#!/usr/bin/env python3
"""Private Xbox asset preparation. No retail files are downloaded or distributed.

inventory --ps2-vol RAMPAGE.VOL [--wii EXTRACTED_DIR] --out report.json
optimize --input ORIGINAL.cmg --output PRIVATE/OPTIMIZED.cmg --texture-scale .5 --deduplicate

Uses the existing Wii readers and writes the same Wii bundle format. It does not
convert to PS2 format, export assets publicly, or alter the running game.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import PIL

import bdp
import mesh
import pvol
import tex

MONSTERS = ('Boris', 'Ruby', 'Plucky', 'Fifi', 'Cyril', 'Squirmy', 'Flea', 'Sarah', 'Brian', 'Vern')
SCHEMA = 1


def digest(data):
    return hashlib.sha256(data).hexdigest()


def provenance():
    root = Path(__file__).resolve().parent
    return dict(python=sys.version.split()[0],pillow=PIL.__version__,
                tools_sha256={name:digest((root/name).read_bytes()) for name in
                              ('xbox_assets.py','bdp.py','mesh.py','tex.py','pvol.py')})


def align(out, n=32):
    out.extend(b'\0' * (-len(out) % n))
    return len(out)


def write_new(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as f:
        f.write(data)


def json_bytes(obj):
    return (json.dumps(obj, indent=2, sort_keys=True) + '\n').encode()


def runtime_index_capacity(b, data, data2, etype, part, section):
    """Actual src/mesh.c index allocation capacity, excluding allocator overhead."""
    if b.endian == '<':
        return section.count  # PS2 allocates 12*nverts, not 12*ntris.
    recs = wii_records(etype, data)
    rec, block, desc = recs[part]
    _, narrow, at, size, attrs = struct.unpack_from('>5I', data, block)
    p, end, count, capacity = at, at + size, 0, 0
    while p < end and data2[p]:
        op, n = struct.unpack_from('>BH', data2, p)
        p += 3 + n * attrs * (1 if narrow else 2)
        more = n // 4 * 2 if op == 0x80 else n // 3 if op == 0x90 else max(0, n - 2)
        if count + more > capacity:
            capacity = (count + more) * 2 + 16
        count += more
    return capacity


def metrics(raw, name):
    """Decoded data accounting, not a measured whole-scene working set."""
    b = bdp.Bundle(raw)
    out = dict(name=name, decoded_metrics_scope='segment 0, as loaded by the current native C reader', platform='Wii' if b.endian == '>' else 'PS2', sha256=digest(raw),
               file_bytes=len(raw), textures=[], meshes=[], skeletons=[], animations=0,
               animation_payload_bytes=0, cloth_entries=0, material_entries=0,
               vertices=0, triangles=0, mesh_array_bytes=0, mesh_part_count=0,
               texture_rgba_base_bytes=0, texture_rgba_full_mip_bytes=0)
    for e, data, data2 in b.items():
        entryname = b.name(e.name).lstrip('\x13')
        if e.type == 9:
            t = tex.wii_header(data) if b.endian == '>' else tex.header(data)
            w, h = t['width'], t['height']
            if not w or not h:
                raise ValueError(f'{name}: zero texture dimension: {entryname}')
            mip_bytes, mw, mh = 0, w, h
            while True:
                mip_bytes += 4 * mw * mh
                if mw == mh == 1:
                    break
                mw, mh = max(1, mw // 2), max(1, mh // 2)
            out['textures'].append(dict(name=entryname, width=w, height=h,
                storage_format=t.get('format', t.get('psm')), stored_bytes=len(data2),
                rgba_base_bytes=4*w*h, rgba_full_mip_bytes=mip_bytes))
            out['texture_rgba_base_bytes'] += 4*w*h
            out['texture_rgba_full_mip_bytes'] += mip_bytes
        elif e.type in (0, 17):
            row = dict(name=entryname, skinned=e.type == 17, vertices=0, triangles=0,
                       array_bytes=0, parts=0, max_influences=0)
            for pi, part in enumerate(mesh.read_mesh(e.type, data, data2, b.endian)):
                for s in part.sections:
                    n, triangles = s.count, len(mesh.triangles(s))
                    stride = 12 + (12 if s.normals else 0) + (8 if s.uvs else 0) + (16 if s.colours else 0)
                    if e.type == 17:
                        stride += 20  # uint8[4] joints and float[4] weights, on both platforms.
                    capacity = runtime_index_capacity(b, data, data2, e.type, pi, s)
                    row['vertices'] += n
                    row['triangles'] += triangles
                    row['array_bytes'] += n * stride + 12 * capacity
                    row['parts'] += 1
                    row['max_influences'] = max(row['max_influences'],
                        (4 if s.format & mesh.FOUR_BONES else 2) if e.type == 17 and b.endian == '>'
                        else 2 if e.type == 17 else 0)
            out['meshes'].append(row)
            for k in ('vertices', 'triangles'):
                out[k] += row[k]
            out['mesh_array_bytes'] += row['array_bytes']
            out['mesh_part_count'] += row['parts']
        elif e.type == 3:
            out['skeletons'].append(dict(name=entryname, nodes=struct.unpack_from(b.endian+'I', data, 0x20)[0]))
        elif e.type == 4:
            out['animations'] += 1
            out['animation_payload_bytes'] += len(data) + len(data2)
        elif e.type == 25:
            out['cloth_entries'] += 1
        elif e.type == 6:
            out['material_entries'] += 1
    # 32-bit Xbox Bundle: names plus three name-index maps; BdpEntry is 32 bytes.
    out['bundle_index_bytes_xbox32'] = 16*len(b.names) + 32*len(b.segments[0].entries)
    out['mesh_part_struct_bytes_xbox32'] = 48*out['mesh_part_count']
    return out


def wii_root(path):
    path = Path(path)
    for candidate in (path, path/'DATA'/'files', path/'files'):
        if (candidate/'Game').is_dir():
            return candidate
    raise ValueError('Wii input must contain Game, DATA/files/Game, or files/Game')


def summarize(rows):
    keys = ('file_bytes', 'bundle_index_bytes_xbox32', 'vertices', 'triangles', 'mesh_array_bytes',
            'mesh_part_struct_bytes_xbox32', 'texture_rgba_base_bytes', 'texture_rgba_full_mip_bytes',
            'animation_payload_bytes', 'animations', 'material_entries', 'cloth_entries')
    return {k: sum(r[k] for r in rows) for k in keys}


def inventory(ps2_vol=None, wii=None):
    rows = []
    if ps2_vol:
        with open(ps2_vol, 'rb') as f:
            for name, offset, size in pvol.read_index(f):
                if Path(name).suffix.lower() not in bdp.EXTENSIONS:
                    continue
                f.seek(offset)
                raw = f.read(size)
                if len(raw) != size:
                    raise ValueError(f'short VOL read: {name}')
                rows.append(metrics(raw, name))
    availability = dict(provided=bool(wii), extras={name: {'model':False, 'animations':False} for name in MONSTERS},
                        dallas_files=[])
    if wii:
        root = wii_root(wii)
        for folder in ('Game', 'Shell', 'Audio'):
            for path in sorted((root/folder).glob('*')):
                if path.suffix.lower() not in ('.bdg', '.cmg') or not path.is_file():
                    continue
                row = metrics(path.read_bytes(), str(path.relative_to(root)))
                rows.append(row)
                for name in MONSTERS:
                    if path.name.lower() == name.lower()+'.cmg':
                        availability['extras'][name]['model'] = True
                    elif path.name.lower() == name.lower()+'_anims.cmg':
                        availability['extras'][name]['animations'] = True
                if 'dallas' in path.name.lower():
                    availability['dallas_files'].append(str(path.relative_to(root)))
    return dict(schema=SCHEMA, provenance=provenance(), wii_availability=availability, bundles=rows,
                totals_by_platform={platform: summarize([r for r in rows if r['platform']==platform])
                                    if any(r['platform']==platform for r in rows) else None
                                    for platform in ('PS2', 'Wii')},
                caveats=[
                    'Asset inventory totals, not simultaneously loaded scene totals or measured peak RAM.',
                    'Texture RGBA costs describe decoder output and potential uploads, not confirmed GPU allocation.',
                    'Separate HUD/3D caches, atlas region textures, POT rounding and GL driver copies may add storage.',
                    'Mesh arrays reflect src/mesh.c capacity rules; instances, skinning copies and generated buildings add memory.',
                    'Archive file bytes are retained by Bundle; animation payload is already included there, not additive.',
                    'Wii figures are present only when actual Wii bundles were supplied.'])


def wii_records(etype, data):
    if etype == 0:
        count, first, stride, bp, dp = struct.unpack_from('>I', data)[0], 0x20, 0x50, 0x3C, 0x40
    elif etype == 17:
        count, bones = struct.unpack_from('>HH', data)
        first, stride, bp, dp = 16+4*bones, 0x58, 0x40, 0x44
    else:
        raise ValueError('not a mesh')
    records = []
    for i in range(count):
        r = first + stride*i
        block, desc = r+struct.unpack_from('>I', data, r+bp)[0], r+struct.unpack_from('>I', data, r+dp)[0]
        if r+stride > len(data) or block+20 > len(data) or desc+24 > len(data):
            raise ValueError('out-of-bounds mesh descriptor')
        records.append((r, block, desc))
    return records


def corner_signature(etype, data, data2):
    """Exact ordered per-corner vertex bytes and materials, including bone weights."""
    signature = hashlib.sha256()
    for r, block, desc in wii_records(etype, data):
        count, stride, fmt, _, at, size = struct.unpack_from('>6I', data, desc)
        material = data[r+(0x38 if etype == 17 else 0x0A):r+(0x3A if etype == 17 else 0x0C)]
        _, narrow, dl, dl_size, attrs = struct.unpack_from('>5I', data, block)
        signature.update(material+struct.pack('>I',fmt))
        for tri in mesh.wii_triangles(data2, dl, dl_size, attrs, not narrow, count):
            for index in tri:
                signature.update(data2[at+stride*index:at+stride*(index+1)])
    return signature.hexdigest()


def deduplicate_mesh(etype, data, data2):
    """Exact complete-vertex dedup within each part; never collapse edges or remove triangles."""
    mesh.read_mesh(etype, data, data2, '>')  # Validate supported layouts first.
    ranges = []
    for rec, block, desc in wii_records(etype, data):
        _,_,_,_,at,size = struct.unpack_from('>6I',data,desc)
        _,_,dl,dl_size,_ = struct.unpack_from('>5I',data,block)
        ranges.extend(((at,at+size),(dl,dl+dl_size)))
    cursor = 0
    for start,end in sorted(ranges)+[(len(data2),len(data2))]:
        gap = data2[cursor:start]
        if start < cursor or (gap.strip(b'\0') and gap.strip(b'\xff')):
            return data,data2,0  # Unknown/shared storage: leave it exactly as supplied.
        cursor = end
    header, out, removed = bytearray(data), bytearray(), 0
    for rec, block, desc in wii_records(etype, data):
        count, stride, fmt, zero, at, size = struct.unpack_from('>6I', data, desc)
        unique, remap, lookup = [], [], {}
        for i in range(count):
            vertex = data2[at+i*stride:at+(i+1)*stride]
            if vertex not in lookup:
                lookup[vertex] = len(unique)
                unique.append(vertex)
            remap.append(lookup[vertex])
        removed += count-len(unique)
        vertices_at = align(out)
        out.extend(b''.join(unique))
        dl_count, narrow, dl_at, dl_size, attrs = struct.unpack_from('>5I', data, block)
        display = bytearray(data2[dl_at:dl_at+dl_size])
        p, width = 0, 1 if narrow else 2
        while p < len(display) and display[p]:
            op, n = struct.unpack_from('>BH', display, p)
            p += 3
            for _ in range(n*attrs):
                old = display[p] if narrow else struct.unpack_from('>H', display, p)[0]
                if narrow:
                    display[p] = remap[old]
                else:
                    struct.pack_into('>H', display, p, remap[old])
                p += width
        display_at = align(out)
        out.extend(display)
        struct.pack_into('>6I', header, desc, len(unique), stride, fmt, zero, vertices_at, len(unique)*stride)
        struct.pack_into('>5I', header, block, dl_count, narrow, display_at, dl_size, attrs)
    if not removed or len(out) > len(data2):
        return data, data2, 0
    header, out = bytes(header), bytes(out)
    if corner_signature(etype,data,data2) != corner_signature(etype,header,out):
        raise ValueError('lossless mesh verification failed')
    return header, out, removed


def encode_gx_rgba8(image):
    """Wii RGBA8 4x4 tiles: AR plane then GB plane; edge padding is clamped."""
    image = image.convert('RGBA')
    w, h = image.size
    px, out = image.load(), bytearray()
    for ty in range(0,h,4):
        for tx in range(0,w,4):
            pixels = [px[min(w-1,tx+x), min(h-1,ty+y)] for y in range(4) for x in range(4)]
            out.extend(bytes(v for r,g,b,a in pixels for v in (a,r)))
            out.extend(bytes(v for r,g,b,a in pixels for v in (g,b)))
    return bytes(out)


def resize_texture(data, data2, scale, max_side):
    t = tex.wii_header(data)
    w, h = t['width'], t['height']
    if w < 1 or h < 1:
        raise ValueError('invalid texture dimensions')
    factor = min(scale, max_side/max(w,h)) if max_side else scale
    if factor >= 1:
        return data, data2, None
    # An exact aspect ratio avoids shifting atlas-region pixel boundaries unevenly.
    # Halving is the supported reduction step; never resize up.
    divisor = 1
    while 1/divisor > factor:
        divisor *= 2
    if w % divisor or h % divisor or min(w,h) < divisor:
        return data, data2, {'skipped':'dimensions do not support an exact power-of-two reduction'}
    target = (w//divisor,h//divisor)
    original = tex.wii_image(data,data2)
    reduced = original.resize(target, resample=tex.Image.Resampling.BOX)
    # RGBA preserves the resized alpha values. We do not guess material alpha modes.
    pixels = encode_gx_rgba8(reduced)
    header = bytearray(data)
    struct.pack_into('>IHHH',header,4,6,*target,1)
    header = bytes(header)
    if tex.wii_image(header,pixels).tobytes() != reduced.tobytes():
        raise ValueError('GX RGBA8 round-trip mismatch')
    before, after = len(data2)+4*w*h, len(pixels)+4*target[0]*target[1]
    if after >= before:
        return data,data2,{'skipped':'candidate does not reduce source payload plus one RGBA upload'}
    return header,pixels,dict(before=[w,h],after=list(target),source_format=t['format'],output_format=6,
         payload_delta_bytes=len(pixels)-len(data2),rgba_base_saved_bytes=4*(w*h-target[0]*target[1]),
         alpha_test_coverage_before=sum(a>=128 for a in original.getchannel('A').tobytes())/(w*h),
         alpha_test_coverage_after=sum(a>=128 for a in reduced.getchannel('A').tobytes())/(target[0]*target[1]))


def rebuild_bundle(b, replacements):
    """Re-layout documented bundle storage; all unmodified entry payloads stay byte-identical.

    Replacement keys are (segment number, record position). Name offsets inside the
    name table, entry IDs/order/types and all entry-local references are preserved.
    """
    out = bytearray(b.data[:0x60])
    names_end = max([4+4*len(b.names)] + [o+len(n.encode('latin1'))+1 for o,n in zip(b.name_offsets,b.names)])
    names_at = align(out)
    out.extend(b.data[b.names_at:b.names_at+names_end])
    struct.pack_into(b.endian+'I',out,0x34,names_at)
    for si,s in enumerate(b.segments):
        header = align(out)
        out.extend(b'\0'*(24+18*len(s.records)))
        struct.pack_into(b.endian+'I',out,0x38+4*si,header)
        entries_at = align(out)
        out.extend(b.data[s.entries_offset:s.entries_offset+16*len(s.entries)])
        items = list(b.items(si))
        data_offsets, data2_offsets = [],[]
        data_at = align(out)
        for j,(_,a,d2) in enumerate(items):
            a,d2 = replacements.get((si,j),(a,d2))
            offset = align(out)-data_at
            data_offsets.append((offset,len(a)))
            out.extend(a)
        data_size = len(out)-data_at
        data2_at = align(out)
        for j,(_,a,d2) in enumerate(items):
            a,d2 = replacements.get((si,j),(a,d2))
            offset = align(out)-data2_at
            data2_offsets.append((offset,len(d2)))
            out.extend(d2)
        data2_size = len(out)-data2_at
        struct.pack_into(b.endian+'HHIIIII',out,header,s.number,len(s.records),entries_at,data_at,data_size,data2_at,data2_size)
        for j,(r,(a,n),(d2,n2)) in enumerate(zip(s.records,data_offsets,data2_offsets)):
            struct.pack_into(b.endian+'HIIII',out,header+24+18*j,r.entry,a,n,d2,n2)
    align(out)
    result = bytes(out)
    verify_rebuild(b,bdp.Bundle(result),replacements)
    return result


def verify_rebuild(before,after,replacements):
    if before.names != after.names or before.absent != after.absent:
        raise ValueError('name table or absent-segment flags changed')
    for si in range(10):
        old,new = list(before.items(si)),list(after.items(si))
        if len(old) != len(new):
            raise ValueError('entry count changed')
        for j,((oe,oa,od),(ne,na,nd)) in enumerate(zip(old,new)):
            if oe != ne or (na,nd) != replacements.get((si,j),(oa,od)):
                raise ValueError(f'payload verification failed at segment {si}, entry {j}')
    bdp.coverage(after)


def optimize(raw,scale=1.0,max_side=None,deduplicate=False):
    if not (math.isfinite(scale) and 0 < scale <= 1):
        raise ValueError('texture scale must be in (0,1]')
    if max_side is not None and max_side < 1:
        raise ValueError('max texture side must be positive')
    b = bdp.Bundle(raw)
    if b.endian != '>':
        raise ValueError('optimization accepts Wii bundles only; PS2 input is inventory-only')
    # Never change a bundle with unexplained bytes or references before understanding it.
    # The reader supplies layout validation; every unchanged entry is verified after rebuilding.
    for start,end,fill in bdp.gaps(b):
        if fill.strip(b'\0') and fill.strip(b'\xff'):
            raise ValueError(f'unexplained non-padding bytes at {start:#x}; refusing to rewrite')
    changes,replacements = [],{}
    has_cloth = any(e.type==25 for si in range(10) for e,a,d in b.items(si))
    for si in range(10):
        for j,(e,a,d2) in enumerate(b.items(si)):
            na,nd = a,d2
            row = dict(segment=si,entry=j,name=b.name(e.name).lstrip('\x13'))
            if e.type==9 and (scale<1 or max_side):
                na,nd,change = resize_texture(a,d2,scale,max_side)
                if change:
                    row['texture'] = change
            elif e.type in (0,17) and deduplicate and not (e.type==0 and has_cloth):
                na,nd,n = deduplicate_mesh(e.type,a,d2)
                if n:
                    row['vertices_removed_losslessly'] = n
            if (na,nd)!=(a,d2):
                replacements[(si,j)] = (na,nd)
            if len(row)>3:
                changes.append(row)
    result = rebuild_bundle(b,replacements) if replacements else raw
    before,after = metrics(raw,'original'),metrics(result,'optimized')
    # Preserve gameplay and all native material/rig/animation/cloth bytes, not a lossy glTF round-trip.
    converted = bdp.Bundle(result)
    for si in range(10):
        for old,new in zip(b.items(si),converted.items(si)):
            if old[0].type not in (0,9,17) and old != new:
                raise ValueError('protected payload changed')
    return result,dict(schema=SCHEMA,provenance=provenance(),source_sha256=digest(raw),output_sha256=digest(result),
        policy=dict(texture_scale=scale,max_texture_side=max_side,deduplicate=deduplicate,
                    rigid_meshes_skipped_for_cloth=has_cloth),changes=changes,
        before=before,after=after,validation='Entry preservation and texture/triangle-corner round trips passed; gameplay untested.')


def main():
    parser = argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command',required=True)
    inv = sub.add_parser('inventory')
    inv.add_argument('--ps2-vol')
    inv.add_argument('--wii')
    inv.add_argument('--out',required=True)
    opt = sub.add_parser('optimize')
    opt.add_argument('--input',required=True)
    opt.add_argument('--output',required=True)
    opt.add_argument('--report')
    opt.add_argument('--texture-scale',type=float,default=1)
    opt.add_argument('--max-texture-side',type=int)
    opt.add_argument('--deduplicate',action='store_true')
    args = parser.parse_args()
    try:
        if args.command=='inventory':
            if not (args.ps2_vol or args.wii):
                parser.error('provide --ps2-vol and/or --wii')
            report = inventory(args.ps2_vol,args.wii)
            write_new(args.out,json_bytes(report))
            print(json.dumps(report['totals_by_platform'],indent=2))
        else:
            if not (math.isfinite(args.texture_scale) and 0<args.texture_scale<=1):
                parser.error('--texture-scale must be greater than zero and no more than one')
            if args.max_texture_side is not None and args.max_texture_side<1:
                parser.error('--max-texture-side must be positive')
            source,destination = Path(args.input).resolve(),Path(args.output).resolve()
            if source==destination or destination.exists():
                parser.error('output must be new and different from input')
            report_path = Path(args.report or str(destination)+'.json')
            if report_path.resolve() in (source,destination) or report_path.exists():
                parser.error('report must be a separate new path')
            result,report = optimize(source.read_bytes(),args.texture_scale,args.max_texture_side,args.deduplicate)
            write_new(destination,result)
            write_new(report_path,json_bytes(report))
            print(json.dumps({'output':str(destination),'report':str(report_path),'changes':len(report['changes'])}))
    except (ValueError, OSError, struct.error, IndexError) as e:
        print(f'asset pipeline: {e}',file=sys.stderr)
        return 1
    return 0


if __name__=='__main__':
    sys.exit(main())
