#!/usr/bin/env python3
"""Optional real-volume fault recovery; adapted from prior full-source fault recipe."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess
from extract import extract
p=argparse.ArgumentParser();p.add_argument('--src',type=Path,required=True);p.add_argument('--volume',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--sanitize',action='store_true');p.add_argument('--low-memory',action='store_true');a=p.parse_args()
src=a.src.resolve();volume=a.volume.resolve();out=a.output.resolve();here=Path(__file__).resolve().parent;f=here/'fullsource';assert volume.is_file();out.mkdir(parents=True,exist_ok=True);assert not (out/'receipts.json').exists();(out/'tmp').mkdir(exist_ok=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
pins={str(p):sha(p) for d in [src,f] for p in d.iterdir() if p.is_file()};(out/'inputs.json').write_text(json.dumps(pins,indent=2))
env=dict(os.environ,TMPDIR=str(out/'tmp'),ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',FAIL_FILE='city.c');rows=[]
def run(cmd,label):
 with (out/(label+'.out')).open('w') as stdout,(out/(label+'.err')).open('w') as stderr:r=subprocess.run(list(map(str,cmd)),stdout=stdout,stderr=stderr,env=env)
 rows.append(dict(label=label,command=list(map(str,cmd)),status=r.returncode,stdout_sha256=sha(out/(label+'.out')),stderr_sha256=sha(out/(label+'.err'))));(out/'receipts.json').write_text(json.dumps(rows,indent=2));assert r.returncode==0,(label,r.returncode)
flags=['-std=c99','-D_DEFAULT_SOURCE','-O1','-g','-ffunction-sections','-fdata-sections'];cc=os.environ.get('CC','cc')
if a.low_memory:flags+=['-DRAMPAGE_LOW_MEMORY']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
run([cc,*flags,'-I'+str(f),'-c',f/'alloc_site.c','-o',out/'alloc_site.o'],'compile-allocator')
units='vol bundle mesh shell game building people cloth water tree anim prb shadow look wrand ik boss parade army vehicle rail edible collide'.split()
run([cc,*flags,'-include',f/'alloc_probe.h','-I'+str(src),'-I'+str(f),f/'probe.c',f/'host_stubs.c',out/'alloc_site.o',*[src/(u+'.c') for u in units],'-Wl,--gc-sections','-lm','-o',out/'probe'],'compile-probe')
s=(src/'city.c').read_text()
def line(function,needle):
 text,start,end=extract(s,function);assert text.count(needle)==1,(function,needle);return s.count('\n',0,start+text.index(needle))+1
reset=[line('part_reset',f'p->{field} = ') for field in ['pos','nrm','uv','col','tris']]
obstacle=line('place_object','int *grown = realloc(s->obstacle_of');boxes=line('obstacle_boxes','float (*boxes)[6] = calloc');order=line('order_add','int *grown = realloc(s->order');slots=line('scene_occupant_slots','struct WindowSlot *grown = realloc(s->slots')
cases=[('build',l,1,f'build-reset-{i}') for i,l in enumerate(reset)]+[('build',obstacle,n,f'build-obstacle-{n}') for n in [1]]+[('build',boxes,1,'build-boxes')]+[('facade',l,1,f'runtime-facade-{i}') for i,l in enumerate(reset)]+[('windows',l,1,f'runtime-windows-{i}') for i,l in enumerate(reset) if i not in (1,2)]+[('order',order,1,'runtime-order-later'),('boxes',boxes,1,'runtime-boxes')]
(out/'cases.json').write_text(json.dumps(cases,indent=2))
for kind,l,n,label in cases:
 run([out/'probe',volume,kind,str(l),str(n),'reserved'],label)
 assert 'INJECTED_ALLOCATION_FAILURE' in (out/(label+'.err')).read_text()
 assert 'PASS injected actual-source owner teardown and clean recovery' in (out/(label+'.out')).read_text()
 print('PASS',label,flush=True)
assert all(sha(Path(p))==v for p,v in pins.items());(out/'complete.json').write_text(json.dumps(dict(passed=True,cases=len(cases),recoveries=len(cases),actual_scene_free=True,source_unchanged=True,sanitize=a.sanitize,low_memory=a.low_memory,volume=str(volume)),indent=2))
