"""Synthetic fixtures only. Never embeds or copies retail assets."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
import bdp
import mesh
import tex
import xbox_assets as xa


def pack_bundle(items, secondary=(), endian='>'):
    names = ['texture','mesh','material','root','clip','opaque']
    names_data = bytearray(struct.pack('<I',len(names))+b'\0'*4*len(names))
    for i,n in enumerate(names):
        struct.pack_into('<I',names_data,4+4*i,len(names_data))
        names_data.extend(n.encode()+b'\0')
    out=bytearray(('Pipeworks Bundle File version 1.'+('4' if endian=='>' else '3')).encode().ljust(0x60,b'\0'))
    struct.pack_into(endian+'II',out,0x2C,0x3FC if secondary else 0x3FE,1400 if endian=='>' else 1300)
    nat=xa.align(out); out.extend(names_data)
    struct.pack_into(endian+'I',out,0x34,nat)
    for s in range(10):
        entries=items if s==0 else list(secondary) if s==1 else []
        at=xa.align(out); out.extend(b'\0'*(24+18*len(entries)))
        struct.pack_into(endian+'I',out,0x38+4*s,at)
        et=xa.align(out)
        for i,(typ,name,a,d) in enumerate(entries):
            out.extend(struct.pack(endian+'HBBIII',i,typ,0,name,1,len(a)))
        da=xa.align(out); dr=[]
        for typ,name,a,d in entries:
            off=xa.align(out)-da; dr.append((off,len(a))); out.extend(a)
        ds=len(out)-da
        d2a=xa.align(out); d2r=[]
        for typ,name,a,d in entries:
            off=xa.align(out)-d2a; d2r.append((off,len(d))); out.extend(d)
        d2s=len(out)-d2a
        struct.pack_into(endian+'HHIIIII',out,at,s,len(entries),et,da,ds,d2a,d2s)
        for i,((o,n),(o2,n2)) in enumerate(zip(dr,d2r)):
            struct.pack_into(endian+'HIIII',out,at+24+18*i,i,o,n,o2,n2)
    xa.align(out)
    return bytes(out)


def texture(w=8,h=8):
    image=tex.Image.new('RGBA',(w,h))
    image.putdata([(x*17%256,y*19%256,123,(x+y)*13%256) for y in range(h) for x in range(w)])
    a=bytearray(0x68); struct.pack_into('>IIHHHH',a,0,1,6,w,h,1,0xFFFF)
    return bytes(a),xa.encode_gx_rgba8(image)


def model(skinned=False,four=False,narrow=True,primitive=0x90):
    typ=17 if skinned else 0
    fmt=0x110 | (0x008 if skinned else 0) | (0x004 if four else 0)
    vertices=[]
    for pos in ((0,0,0),(1,0,0),(0,1,0),(0,0,0)):
        v=struct.pack('>3f',*pos)
        if skinned:
            v+=struct.pack('>3f4H',.5,.25,.125,0,1,2,3) if four else struct.pack('>f2H',.5,0,1)
        v+=struct.pack('>3f2f',0,0,1,pos[0],pos[1])
        vertices.append(v)
    stride=len(vertices[0]); data2=bytearray(b''.join(vertices)); dl=xa.align(data2)
    indices=[0,1,2,3,1,2] if primitive==0x90 else [0,1,2,3]
    attrs=3
    display=bytearray(struct.pack('>BH',primitive,len(indices)))
    for index in indices:
        display.extend(struct.pack('>BBB' if narrow else '>HHH',index,index,index))
    data2+=display
    first=16 if skinned else 0x20
    rsize=0x58 if skinned else 0x50
    block=first+rsize; desc=block+20
    a=bytearray(desc+24)
    if skinned: struct.pack_into('>HH',a,0,1,0)
    else: struct.pack_into('>I',a,0,1)
    struct.pack_into('>H',a,first+(0x38 if skinned else 0x0A),2)
    struct.pack_into('>II',a,first+(0x40 if skinned else 0x3C),block-first,desc-first)
    struct.pack_into('>5I',a,block,len(indices),int(narrow),dl,len(display),attrs)
    struct.pack_into('>6I',a,desc,len(vertices),stride,fmt,0,0,stride*len(vertices))
    return typ,bytes(a),bytes(data2)


def multiple_parts(shared=False):
    typ,source,pixels=model()
    header=bytearray(0x20+2*0x50+2*44)
    struct.pack_into('>I',header,0,2)
    payload=bytearray()
    for i in range(2):
        rec=0x20+i*0x50;block=0xC0+i*44;desc=block+20
        header[rec:rec+0x50]=source[0x20:0x70]
        struct.pack_into('>H',header,rec+0x0A,2+i)
        struct.pack_into('>II',header,rec+0x3C,block-rec,desc-rec)
        base=0 if shared else xa.align(payload)
        if not shared or i==0:payload.extend(pixels)
        dl=list(struct.unpack_from('>5I',source,0x70));dl[2]+=base
        vd=list(struct.unpack_from('>6I',source,0x84));vd[4]+=base
        struct.pack_into('>5I',header,block,*dl)
        struct.pack_into('>6I',header,desc,*vd)
    return bytes(header),bytes(payload)


def skeleton(n=4):
    offsets=[]; at=0x40
    for i in range(n):
        offsets.append(at);at+=20 if i+1<n else 16
    table,by_name,poses=at,at+4*n,at+8*n
    a=bytearray(poses+80*n)
    struct.pack_into('>8I',a,0x1C,0x40,n,n,len(a),table,by_name,0,0)
    struct.pack_into('>I',a,0x3C,poses)
    for i,at in enumerate(offsets):
        struct.pack_into('>iiII',a,at,i,i-1,int(i+1<n),3)
        if i+1<n:struct.pack_into('>I',a,at+16,offsets[i+1])
        struct.pack_into('>I',a,table+4*i,at)
        struct.pack_into('>I',a,by_name+4*i,at)
        struct.pack_into('>I19f',a,poses+80*i,0,1,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,1)
    return bytes(a)


def animation(n=4):
    translations,rotations=0x48,0x48+32*n
    a=bytearray(rotations+18*n)
    struct.pack_into('>fIIHHIIIIII',a,0x1C,1.0,2,len(a),3,0,n,n,0x44,translations,rotations,136*n)
    struct.pack_into('>I',a,0x44,(1<<(2*n))-1)
    for i in range(n):
        struct.pack_into('>3fBBBB',a,translations+32*i,1,1,1,i,2,1,0)
        struct.pack_into('>HhhhHhhh',a,translations+32*i+16,0,0,0,0,65534,100*i,100,0)
        struct.pack_into('>BBHhhhHhhh',a,rotations+18*i,i,2,0,0,0,0,65534,100*i,0,0)
    return bytes(a)


def fixture(skinned=False,four=False,narrow=True,cloth=False,texture_format=6,primitive=0x90):
    ta,td=texture()
    if texture_format!=6:
        ta=bytearray(ta);struct.pack_into('>I',ta,4,texture_format);ta=bytes(ta)
        size=tex.wii_size(tex.wii_header(ta))
        td=bytes((i*37+11)%256 for i in range(size))
    typ,ma,md=model(skinned,four,narrow,primitive)
    mat=bytearray(0x70);mat[2]=2;struct.pack_into('>H',mat,0x38,0)
    entries=[(9,0,ta,td),(typ,1,ma,md),(6,2,bytes(mat),b''),
             (3,3,skeleton(),b''),(4,4,animation(),b''),(2,5,b'gameplay remains byte identical',b'extra opaque payload')]
    if cloth:
        entries.append((25,5,b'\0'*0x4C,b''))
    return pack_bundle(entries,[(2,5,b'other segment',b'other segment data2')])


class PipelineTests(unittest.TestCase):
    def test_gx_roundtrip_odd_tiles(self):
        for w,h in ((1,1),(3,5),(8,8),(16,4)):
            with self.subTest(size=(w,h)):
                image=tex.Image.new('RGBA',(w,h))
                image.putdata([(i%256,i*3%256,i*7%256,i*11%256) for i in range(w*h)])
                a=bytearray(0x68);struct.pack_into('>IIHHH',a,0,1,6,w,h,1)
                self.assertEqual(tex.wii_image(a,xa.encode_gx_rgba8(image)).tobytes(),image.tobytes())

    def test_all_documented_gx_input_formats(self):
        for fmt in (0,1,3,5,6,14):
            with self.subTest(format=fmt):
                raw=fixture(texture_format=fmt)
                output,report=xa.optimize(raw,scale=.5)
                old=list(bdp.Bundle(raw).items())[0]
                new=list(bdp.Bundle(output).items())[0]
                image=tex.wii_image(*old[1:]).resize((4,4),resample=tex.Image.Resampling.BOX)
                self.assertEqual(tex.wii_image(*new[1:]).tobytes(),image.tobytes())

    def test_rebuild_preserves_all_entries_and_segments(self):
        raw=fixture()
        rebuilt=xa.rebuild_bundle(bdp.Bundle(raw),{})
        xa.verify_rebuild(bdp.Bundle(raw),bdp.Bundle(rebuilt),{})
        self.assertEqual(list(bdp.Bundle(raw).items(1)),list(bdp.Bundle(rebuilt).items(1)))

    def test_untouched_policy_is_byte_identical(self):
        raw=fixture()
        out,report=xa.optimize(raw)
        self.assertEqual(raw,out)
        self.assertEqual(report['changes'],[])

    def test_texture_reduction_and_protected_records(self):
        raw=fixture()
        out,report=xa.optimize(raw,scale=.5)
        self.assertEqual(report['after']['texture_rgba_base_bytes'],64)
        self.assertEqual(report['before']['texture_rgba_base_bytes'],256)
        self.assertEqual(report['after']['animations'],1)
        self.assertEqual(report['after']['material_entries'],1)
        for old,new in zip(bdp.Bundle(raw).items(),bdp.Bundle(out).items()):
            if old[0].type!=9: self.assertEqual(old,new)

    def test_mesh_dedup_all_joint_layouts_and_index_widths(self):
        for skinned,four in ((False,False),(True,False),(True,True)):
            for narrow in (False,True):
                with self.subTest(skinned=skinned,four=four,narrow=narrow):
                    raw=fixture(skinned,four,narrow)
                    out,report=xa.optimize(raw,deduplicate=True)
                    self.assertEqual(report['before']['vertices'],4)
                    self.assertEqual(report['after']['vertices'],3)
                    self.assertEqual(report['before']['triangles'],report['after']['triangles'])
                    old=list(bdp.Bundle(raw).items())[1]; new=list(bdp.Bundle(out).items())[1]
                    self.assertEqual(xa.corner_signature(old[0].type,*old[1:]),xa.corner_signature(new[0].type,*new[1:]))

    def test_all_supported_primitive_types(self):
        for primitive in (0x80,0x90,0x98,0xA0):
            with self.subTest(primitive=primitive):
                typ,a,d=model(primitive=primitive)
                na,nd,removed=xa.deduplicate_mesh(typ,a,d)
                self.assertEqual(removed,1)
                self.assertEqual(xa.corner_signature(typ,a,d),xa.corner_signature(typ,na,nd))

    def test_material_parts_remain_separate(self):
        a,d=multiple_parts()
        na,nd,removed=xa.deduplicate_mesh(0,a,d)
        parts=mesh.read_mesh(0,na,nd,'>')
        self.assertEqual(removed,2)
        self.assertEqual([p.material for p in parts],[2,3])
        self.assertEqual([p.sections[0].count for p in parts],[3,3])

    def test_different_uv_seam_is_not_merged(self):
        typ,a,d=model()
        changed=bytearray(d)
        stride=struct.unpack_from('>I',a,0x88)[0]
        struct.pack_into('>f',changed,3*stride+stride-8,.5)
        self.assertEqual(xa.deduplicate_mesh(typ,a,bytes(changed)),(a,bytes(changed),0))

    def test_shared_mesh_storage_is_not_repacked(self):
        a,d=multiple_parts(shared=True)
        self.assertEqual(xa.deduplicate_mesh(0,a,d),(a,d,0))

    def test_unknown_mesh_payload_is_not_dropped(self):
        typ,a,d=model();d+=b'undocumented mesh payload'
        self.assertEqual(xa.deduplicate_mesh(typ,a,d),(a,d,0))

    def test_cloth_rigid_mesh_not_changed(self):
        raw=fixture(cloth=True)
        out,report=xa.optimize(raw,deduplicate=True)
        self.assertEqual(raw,out)
        self.assertTrue(report['policy']['rigid_meshes_skipped_for_cloth'])

    def test_secondary_segment_cloth_blocks_rigid_dedup(self):
        typ,a,d=model()
        raw=pack_bundle([(typ,1,a,d)],[(25,5,b'\0'*0x4C,b'')])
        out,report=xa.optimize(raw,deduplicate=True)
        self.assertEqual(out,raw)
        self.assertTrue(report['policy']['rigid_meshes_skipped_for_cloth'])

    def test_odd_dimension_reduction_skipped(self):
        a,d=texture(7,5)
        na,nd,reason=xa.resize_texture(a,d,.5,None)
        self.assertEqual((a,d),(na,nd))
        self.assertIn('skipped',reason)

    def test_ps2_optimization_refused(self):
        raw=pack_bundle([(2,5,b'opaque',b'')],endian='<')
        self.assertEqual(bdp.Bundle(raw).endian,'<')
        with self.assertRaisesRegex(ValueError,'Wii bundles only'):
            xa.optimize(raw,scale=.5)

    def test_unexplained_bytes_refused(self):
        raw=fixture()+b'undocumented payload'
        with self.assertRaisesRegex(ValueError,'unexplained non-padding'):
            xa.optimize(raw,scale=.5)

    def test_cli_never_overwrites(self):
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/'source.cmg';src.write_bytes(fixture())
            before=src.read_bytes()
            p=subprocess.run([sys.executable,str(ROOT/'tools/xbox_assets.py'),'optimize','--input',str(src),'--output',str(src),'--texture-scale','.5'],capture_output=True)
            self.assertNotEqual(p.returncode,0)
            self.assertEqual(src.read_bytes(),before)

    def test_cli_output_deterministic(self):
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/'source.cmg';src.write_bytes(fixture(skinned=True,four=True))
            for i in (1,2):
                p=subprocess.run([sys.executable,str(ROOT/'tools/xbox_assets.py'),'optimize','--input',str(src),'--output',str(Path(tmp)/f'out{i}.cmg'),'--texture-scale','.5','--deduplicate'],capture_output=True)
                self.assertEqual(p.returncode,0,p.stderr.decode())
            self.assertEqual((Path(tmp)/'out1.cmg').read_bytes(),(Path(tmp)/'out2.cmg').read_bytes())
            self.assertEqual(json.loads((Path(tmp)/'out1.cmg.json').read_text()),json.loads((Path(tmp)/'out2.cmg.json').read_text()))


if __name__=='__main__':
    unittest.main()
