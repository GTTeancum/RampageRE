#!/usr/bin/env python3
"""Build actual C readers, then check synthetic Wii output and optional PS2 VOL."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import sys

from test_pipeline import ROOT, fixture, multiple_parts, pack_bundle
sys.path.insert(0,str(ROOT/'tools'))
import pvol
import bdp
import tex
import xbox_assets as xa


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ps2-vol')
    parser.add_argument('--sanitize',action='store_true')
    args=parser.parse_args()
    checks=0
    with tempfile.TemporaryDirectory(prefix='rampage-asset-check-') as tmp:
        temp=Path(tmp);exe=temp/'reader_probe'
        flags=['-std=c99','-D_DEFAULT_SOURCE','-O1','-g','-Wall','-Wextra']
        if args.sanitize: flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        env=os.environ.copy()
        if args.sanitize:
            # LSan cannot run in this executor's ptrace sandbox. ASan/UBSan remain on.
            env['ASAN_OPTIONS']=env.get('ASAN_OPTIONS','')+':detect_leaks=0'
            env['UBSAN_OPTIONS']=env.get('UBSAN_OPTIONS','')+':halt_on_error=1'
        subprocess.run([os.environ.get('CC','cc'),*flags,'-I'+str(ROOT/'src'),
            str(Path(__file__).with_name('reader_probe.c')),
            *[str(ROOT/'src'/f) for f in ('bundle.c','mesh.c','tex.c','tree.c','anim.c')],'-lm','-o',str(exe)],check=True)
        def probe(raw,name):
            path=temp/name;path.write_bytes(raw)
            run=subprocess.run([str(exe),str(path)],capture_output=True,text=True,env=env)
            if run.returncode:
                raise RuntimeError(f'C reader failed ({run.returncode}): {run.stderr}')
            result=json.loads(run.stdout)
            bundle=bdp.Bundle(raw)
            if bundle.endian=='>':
                expected=14695981039346656037
                for entry,data,pixels in bundle.items():
                    if entry.type!=9:continue
                    for byte in tex.wii_image(data,pixels).tobytes():
                        expected=((expected^byte)*1099511628211)&0xFFFFFFFFFFFFFFFF
                assert result['pixels']==f'{expected:016x}',(name,'C/Python pixel mismatch')
            return result
        for skinned,four in ((False,False),(True,False),(True,True)):
            for narrow in (False,True):
                raw=fixture(skinned,four,narrow)
                out,report=xa.optimize(raw,scale=.5,deduplicate=True)
                before,after=probe(raw,'before.cmg'),probe(out,'after.cmg')
                assert before['corners']==after['corners']
                assert before['pose']==after['pose']
                assert before['vertices']==4 and after['vertices']==3
                assert before['triangles']==after['triangles']==2
                assert before['rgba_base_bytes']==256 and after['rgba_base_bytes']==64
                assert before['clips']==after['clips']==1 and before['trees']==after['trees']==1
                # No-op conversion and texture decoder agree byte for byte too.
                same,_=xa.optimize(raw)
                assert probe(same,'same.cmg')==before
                checks+=1
        for fmt in (0,1,3,5,6,14):
            raw=fixture(texture_format=fmt)
            out,report=xa.optimize(raw,scale=.5)
            before,after=probe(raw,'format-before.cmg'),probe(out,'format-after.cmg')
            assert before['rgba_base_bytes']==256 and after['rgba_base_bytes']==64
            assert before['pose']==after['pose']
            assert before['corners']==after['corners']
            checks+=1
        for primitive in (0x80,0x98,0xA0):
            raw=fixture(primitive=primitive)
            out,_=xa.optimize(raw,deduplicate=True)
            before,after=probe(raw,'primitive-before.cmg'),probe(out,'primitive-after.cmg')
            assert before['corners']==after['corners']
            assert before['triangles']==after['triangles']==2
            checks+=1
        a,d=multiple_parts()
        raw=pack_bundle([(0,1,a,d)])
        out,_=xa.optimize(raw,deduplicate=True)
        before,after=probe(raw,'parts-before.bdg'),probe(out,'parts-after.bdg')
        assert before['corners']==after['corners']
        assert before['vertices']==8 and after['vertices']==6
        checks+=1
        if args.ps2_vol:
            with open(args.ps2_vol,'rb') as f:
                index=pvol.read_index(f)
                for name in ('George.cmp','George_anims.cmp','LasVegas.bdp'):
                    offset,size=next((offset,size) for n,offset,size in index if n==name)
                    f.seek(offset);raw=f.read(size)
                    c=probe(raw,name);py=xa.metrics(raw,name)
                    assert c['vertices']==py['vertices'],(name,c,py['vertices'])
                    assert c['triangles']==py['triangles'],(name,c,py['triangles'])
                    assert c['rgba_base_bytes']==py['texture_rgba_base_bytes'],(name,c,py['texture_rgba_base_bytes'])
                    assert c['clips']==py['animations']
                    print(name,json.dumps(c,sort_keys=True))
                    checks+=1
        print(f'PASS: {checks} reader cases (synthetic Wii plus optional user-owned PS2).')


if __name__=='__main__':
    main()
