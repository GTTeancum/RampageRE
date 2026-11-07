#!/usr/bin/env python3
"""Current-source-only test: no baseline/fallback functions or private path dependencies."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,tempfile,sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from city_status_extract import write_support
from extract import extract
p=argparse.ArgumentParser();p.add_argument('--src',type=Path,default=Path(__file__).resolve().parents[2]/'src');p.add_argument('--output',type=Path);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
src=a.src.resolve();here=Path(__file__).resolve().parent
out=a.output.resolve() if a.output else Path(tempfile.mkdtemp(prefix='runtime-geometry-',dir=os.environ.get('TMPDIR',src.parent)))
out.mkdir(parents=True,exist_ok=True);assert not (out/'receipts.json').exists(),'Use a fresh output directory; old receipts are preserved.'
(out/'tmp').mkdir(exist_ok=True);env=dict(os.environ,TMPDIR=str(out/'tmp'),ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
pins={str(f):sha(f) for d in [src,here] for f in d.iterdir() if f.is_file()};(out/'inputs.json').write_text(json.dumps(pins,indent=2))
write_support(src/'city.c', out)
s=(src/'city.c').read_text();code='';extracts=[]
for name in ['scene_fail','order_add','scene_occupant_pad','scene_occupant_slots','part_reset','part_quad','face_point','face_normal','cell_rect','facade_passes','open_windows','obstacle_boxes']:
 text,start,end=extract(s,name);line=s.count('\n',0,start)+1;code+=f'\n#line {line} "{src}/city.c"\n'+text+'\n';extracts.append(dict(function=name,line=line,sha256=hashlib.sha256(text.encode()).hexdigest()))
text,_,_=extract(s,'place_object');anchor='if (id >= 0 && flags && strstr(flags, "IsObstacle"))';assert text.count(anchor)==1
start=text.index('{',text.index(anchor))+1;end=text.index('\n    }',start);body=text[start:end];assert body.count('realloc')==1 and 's->obstacle_of[s->nobstacles++] = id;' in body
code+='\nstatic void obstacle_add(Scene *s,int id) {\n'+body+'\n}\n';code+=extract((src/'mesh.c').read_text(),'mesh_free')[0]+'\n'
(out/'current.inc').write_text(code);(out/'extracts.json').write_text(json.dumps(extracts,indent=2))
checks={};main=(src/'main.c').read_text()
def ck(name,text,pattern):checks[name]=bool(re.search(pattern,text,re.S));assert checks[name],name
for name in ['scene_occupant_slots','scene_occupant_pad','scene_people_spawn','scene_buildings_update']:
 receiver=r'\(Viewer \*\)v' if name in ('scene_occupant_slots','scene_people_spawn') else 'v'
 ck('main '+name,main,name+r'\(&v->scene,[^;]*;\s*if \(scene_construction_stop\('+receiver+r'\)\)\s*return;')
ck('free camera',main,r'scene_buildings_update\(&v.scene,[^;]*;\s*if \(scene_construction_stop\(&v\)\)\s*continue;')
ck('initial boxes',s,r'obstacle_boxes\(s\);\s*if \(scene_failed\(s\)\) goto failed;')
ck('initial facade',s,r'facade_passes\(s, sb\);\s*if \(scene_failed\(s\)\) return -2;\s*adornments_place')
ck('runtime windows',s,r'open_windows\(s, sb\);[^\n]*\n\s*if \(scene_failed\(s\)\) return;')
ck('runtime facade',s,r'sb->b.dirty = 0;\s*facade_passes\(s, sb\);\s*if \(scene_failed\(s\)\) return;')
ck('moving facade',s,r'if \(moving \|\| relit\)\s*facade_passes\(s, sb\);\s*if \(scene_failed\(s\)\) return;')
(out/'boundaries.json').write_text(json.dumps(checks,indent=2));rows=[]
def run(cmd,label):
 r=subprocess.run(list(map(str,cmd)),env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE);(out/(label+'.out')).write_bytes(r.stdout);(out/(label+'.err')).write_bytes(r.stderr);rows.append(dict(command=list(map(str,cmd)),label=label,status=r.returncode));(out/'receipts.json').write_text(json.dumps(rows,indent=2));assert r.returncode==0,(label,r.stderr.decode());return r.stdout.decode()
for layout in ['ordinary','low']:
 flags=['-O1','-g','-Wall','-Wextra','-Werror','-D_DEFAULT_SOURCE','-std=c99','-ffunction-sections','-fdata-sections']
 if layout=='low':flags+=['-DRAMPAGE_LOW_MEMORY']
 if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 cc=os.environ.get('CC','cc');exe=out/layout
 run([cc,*flags,'-I'+str(src),'-I'+str(out),here/'regression.c','-Wl,--gc-sections','-lm','-o',exe],layout+'-compile')
 run([cc,*flags,'-I'+str(src),'-fsyntax-only',src/'city.c'],layout+'-syntax')
 log=run([exe,'success',out/(layout+'.bin')],layout+'-success')
 assert sha(out/(layout+'.bin'))=='4ae3c70ec99b9345d984cf6e299cfab000d91e17aced4dfbce5a447049ad016a'
 expected='calls=204 owners=194 freed=194 peak=20696' if layout=='low' else 'calls=201 owners=191 freed=191 peak=12200';assert expected in log,log
 for kind,n in [('facade',13),('facade_unlit',12),('windows',3),('order',1),('slots',2),('slots_later',2),('obstacle',1),('boxes',1)]:
  for fail in range(1,n+1):run([exe,kind,str(fail)],f'{layout}-{kind}-{fail}')
assert all(sha(Path(f))==v for f,v in pins.items())
(out/'complete.json').write_text(json.dumps(dict(passed=True,layouts=2,faults=70,success_fixtures=2,geometry_sha256=sha(out/'ordinary.bin'),source_unchanged=True,sanitized=a.sanitize),indent=2))
print('PASS current-source geometry: 70 faults, 2 exact fixtures, 2 syntax checks, 10 caller checks; outputs retained:',out)
