"""Compile the entire selected current director.c; fault every owned allocation."""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys,tempfile,time,resource
HERE=Path(__file__).resolve().parent
SRC=Path(sys.argv[1]).resolve();MODE=sys.argv[2];assert MODE in ('normal','sanitize')
DIRECTOR=Path(os.environ.get('DIRECTOR_SOURCE_DIR',str(SRC))).resolve()
assert os.environ.get('TMPDIR'), 'Set TMPDIR to a writable directory; all evidence is retained'
OUT=Path(tempfile.mkdtemp(prefix='dir-graph-'+MODE+'-',dir=os.environ['TMPDIR']));(OUT/'tmp').mkdir()
print('Retained evidence: '+str(OUT),flush=True)
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def pins():
 return {str(p):sha(p) for p in sorted(set([*SRC.glob('*.c'),*SRC.glob('*.h'),DIRECTOR/'director.c',DIRECTOR/'director.h']))}
before=pins();(OUT/'source-before.json').write_text(json.dumps(before,indent=2)+'\n')
source=(DIRECTOR/'director.c').read_text()
# The healthy reference removes only the three known allocation checks from the current source.
# Reject an unexpected shape instead of silently comparing the wrong code.
guards=['    if (!ag->action || !ag->fallback)\n        return dir_graph_free(ag), -1;\n',
        '    if (!ag->hop)\n        return dir_graph_free(ag), -1;\n',
        '    if (!ag->cost || !ag->pred || !ag->mark || !ag->queue)\n        return dir_graph_free(ag), -1;\n']
reference=source
for guard in guards:
 assert reference.count(guard)==1,'Expected integrated allocation guard exactly once'
 reference=reference.replace(guard,'',1)
(OUT/'reference-director.c').write_text(reference)
main=(SRC/'main.c').read_text()
load=re.findall(r'^    mo->have_ag = dir_graph_read[^\n]+\n    dir_ai_init\(&mo->ai\);\n    mo->is_ai = 0;',main,re.M)
dispatch=re.findall(r'^        int switched = ev == 0 \|\| \(mo->is_ai && mo->have_ag \?.*?moves_step\(&mo->run, in, cdt, answer, mo\)\);',main,re.M|re.S)
assert len(load)==len(dispatch)==1,'Expected one real load and guarded AI dispatch'
(OUT/'caller.inc').write_text((HERE/'caller-template.inc').read_text().replace('@LOAD@',load[0]).replace('@DISPATCH@',dispatch[0]))
(OUT/'caller-excerpts.json').write_text(json.dumps(dict(load=load[0],dispatch=dispatch[0],source=str(SRC/'main.c'),sha256=sha(SRC/'main.c')),indent=2)+'\n')
(OUT/'probe.c').write_text((HERE/'probe.c').read_text())
flags=['-O2'] if MODE=='normal' else ['-O1','-g1','-fsanitize=address,undefined','-fno-omit-frame-pointer']
env=dict(os.environ,TMPDIR=str(OUT/'tmp'),ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
receipts=[]
def limits():resource.setrlimit(resource.RLIMIT_CORE,(0,0));resource.setrlimit(resource.RLIMIT_CPU,(45,45))
def run(cmd,label):
 t=time.monotonic()
 with (OUT/(label+'.out')).open('x') as out,(OUT/(label+'.err')).open('x') as err:
  r=subprocess.run(list(map(str,cmd)),stdout=out,stderr=err,env=env,timeout=90,preexec_fn=limits)
 receipts.append(dict(label=label,command=list(map(str,cmd)),status=r.returncode,seconds=time.monotonic()-t,stdout_sha256=sha(OUT/(label+'.out')),stderr_sha256=sha(OUT/(label+'.err'))))
 (OUT/'receipts.json').write_text(json.dumps(receipts,indent=2)+'\n');assert r.returncode==0,(label,r.returncode)
 return (OUT/(label+'.out')).read_text()
# Asset args are optional: no proprietary assets are copied or required for synthetic coverage.
args=json.loads(os.environ.get('DIR_GRAPH_ASSET_ARGS','[]'));assert isinstance(args,list)
results={}
for variant,selected in [('reference',OUT/'reference-director.c'),('candidate',DIRECTOR/'director.c')]:
 exe=OUT/variant
 cmd=[os.environ.get('CC','cc'),'-pipe','-std=gnu11','-D_DEFAULT_SOURCE','-D_FILE_OFFSET_BITS=64','-Wall','-Wextra',*flags,'-ffunction-sections','-fdata-sections','-I'+str(DIRECTOR),'-I'+str(SRC),'-I'+str(OUT),'-DSOURCE_FILE="'+str(selected)+'"',OUT/'probe.c',*[SRC/n for n in ('moves.c','bundle.c','vol.c','wrand.c')],'-Wl,--gc-sections','-lm','-o',exe]
 run(cmd,'build-'+variant);text=run([exe,'--'+variant,*args],'run-'+variant);results[variant]=text
assert [x for x in results['reference'].splitlines() if x.startswith('HEALTHY')]==[x for x in results['candidate'].splitlines() if x.startswith('HEALTHY')]
assert before==pins(),'Current source changed during the run'
(OUT/'source-after.json').write_text(json.dumps(before,indent=2)+'\n')
(OUT/'result.json').write_text(json.dumps(dict(passed=True,mode=MODE,source_unchanged=True,healthy_match=True,asset_args=args,tracked_scope='DirGraph allocations only; LeakSanitizer disabled',results={k:[x for x in v.splitlines() if x.startswith(('HEALTHY','FINAL','CALLER','MALFORMED'))] for k,v in results.items()}),indent=2)+'\n')
print(results['candidate'].splitlines()[-2:]);print('PASS '+str(OUT),flush=True)
