from pathlib import Path
import sys,os,json,re,hashlib,tempfile,subprocess
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from city_status_extract import write_support

HERE=Path(__file__).resolve().parent;SRC=Path(sys.argv[1]).resolve();MODE=sys.argv[2];assert MODE in ('normal','sanitize')
for n in ['city.c','main.c','city.h','look.c']:
 if not (SRC/n).is_file():raise SystemExit('Missing live source '+str(SRC/n))
O=Path(tempfile.mkdtemp(prefix='face-pose-'+MODE+'-',dir=os.environ.get('FACE_TEST_OUTPUT_ROOT',os.environ.get('TMPDIR','/tmp'))));(O/'tmp').mkdir();print('Retained:',O,flush=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def pins():return {p.name:sha(p)for p in sorted(SRC.glob('*'))if p.is_file()and p.suffix in ('.c','.h')}
write_support(SRC/'city.c', O)
before=pins();(O/'source-before.json').write_text(json.dumps(before,indent=2));city=(SRC/'city.c').read_text();main=(SRC/'main.c').read_text()
def fn(s,n):
 m=list(re.finditer(r'^(?:static )?[^\n;]*\b'+re.escape(n)+r'\([^;]*?\)\n\{',s,re.M));assert len(m)==1,n
 a=m[0].start();b=s.index('{',a)+1;d=1
 while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
(O/'extracted.inc').write_text('\n'.join(fn(city,n)for n in ['scene_fail','mat_identity','mat_tq','mon_inst','ends_with','mark_subtree','scene_monster_play','scene_monster_face','scene_monster_face_blend','reflect_axis','mirror_pose','scene_monster_mirror']))
a=city.index('        if (s->instances[i].has_clip)',city.index('void scene_free('));b=city.index('        free(s->instances[i].node);',a);(O/'teardown.inc').write_text('int i=0;\n'+city[a:b].replace('s->','s.'))
owners=(HERE/'owners.c').read_text();(O/'owners.c').write_text(owners)
(O/'main.c').write_text(main[:main.index('/* gCommon.getPlayType()')]+'\n'+owners.split('static void healthy(void)')[0]+'\n'+(HERE/'main_tail.c').read_text())
(O/'main-functions.inc').write_text('\n'.join(fn(main,n)for n in ['scene_construction_stop','mon_in','play_state','face_events','face_start','face_play','face_default','face_step','clip_events','mon_fart']))
# Unexecuted outer boundaries are checked explicitly against live source, not represented as runtime coverage.
checks=[('mon_step', 'face_step(mo, dt);', 'if (scene_construction_stop(mo->v)) return;'), ('mon_step', 'play_state(mo);', 'if (scene_construction_stop(v)) return;'), ('mon_step', 'clip_events(mo, to, -1, mo->run.time);', 'if (scene_construction_stop(mo->v)) return;'), ('play_step', 'mon_step(&v->mon[k], dt);', 'if (scene_construction_stop((Viewer *)v)) return;'), ('play_step', 'fx_step(v, dt);', 'if (scene_construction_stop((Viewer *)v)) return;'), ('army_step', 'parade_step(v, dt, m, nm);', 'if (scene_construction_stop((Viewer *)v)) return;'), ('army_step', 'edible_step(v, dt);', 'if (scene_construction_stop((Viewer *)v)) return;'), ('edible_step', 'mon_function(mo, e->name);', 'if (scene_construction_stop((Viewer *)v)) return;'), ('parade_step', 'mon_fart(mo);', 'if (scene_construction_stop((Viewer *)v)) return;'), ('fx_step', 'mon_fart(&v->mon[k]);', 'if (scene_construction_stop((Viewer *)v)) return;'), ('play_script', 'mon_function(mo, name);', 'if (scene_construction_stop((Viewer *)v)) return NULL;\n            if (scene_construction_stop(v)) return *p ? p : NULL;')]
for name,call,guard in checks:
 f=fn(main,name)
 if name=='mon_step':assert 'Viewer *v = mo->v;' in f,'Required equivalent Viewer alias'
 # Whitespace may vary; every required guard statement remains contiguous.
 guard_pattern=r'\s*'.join(re.escape(line.strip()) for line in guard.splitlines())
 assert re.search(re.escape(call)+r'\s*'+guard_pattern,f),(name,call)
(O/'outer-guard-checks.json').write_text(json.dumps(checks,indent=2))
env=dict(os.environ,TMPDIR=str(O/'tmp'),ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1');receipts=[]
def run(cmd,label):
 p=subprocess.run(cmd,env=env,capture_output=True,timeout=120);(O/(label+'.out')).write_bytes(p.stdout);(O/(label+'.err')).write_bytes(p.stderr);receipts.append(dict(label=label,command=cmd,exit=p.returncode));(O/'receipts.json').write_text(json.dumps(receipts,indent=2));assert p.returncode==0,(label,p.stderr[-2000:])
for config in ['ordinary','low-memory']:
 defs=['-DRAMPAGE_LOW_MEMORY']if config=='low-memory'else []
 flags=['-std=gnu11','-D_DEFAULT_SOURCE','-DCANDIDATE','-O1','-ffunction-sections','-fdata-sections']+defs
 if MODE=='sanitize':flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
 for kind in ['owners','main']:
  exe=O/(kind+'-'+config);cmd=[os.environ.get('CC','cc'),*flags,'-I'+str(SRC),str(O/(kind+'.c'))]+([str(SRC/'look.c')]if kind=='main'else [])+['-Wl,--gc-sections','-lm','-o',str(exe)];run(cmd,'build-'+exe.name)
  for arg in (['healthy','faults']if kind=='owners'else [None]):run([str(exe)]+([arg]if arg else []),'run-'+exe.name+('-'+arg if arg else ''))
assert pins()==before,'Source changed';(O/'source-after.json').write_text(json.dumps(before,indent=2));(O/'complete.json').write_text(json.dumps({'passed':True,'mode':MODE,'source':str(SRC),'configurations':2,'harnesses':2,'outer_static_checks':len(checks),'lsan':False},indent=2));print('PASS',flush=True)
