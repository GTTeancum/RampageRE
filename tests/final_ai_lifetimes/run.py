"""Current-source final graph teardown regression. Keeps every run's evidence."""
from pathlib import Path
import hashlib,json,os,re,resource,subprocess,sys,tempfile,time
HERE=Path(__file__).resolve().parent
SRC=Path(sys.argv[1]).resolve(); MODE=sys.argv[2]
assert MODE in ('normal','sanitize')
assert os.environ.get('TMPDIR'),'Set TMPDIR to a private writable evidence directory'
OUT=Path(tempfile.mkdtemp(prefix='final-ai-'+MODE+'-',dir=os.environ['TMPDIR']));(OUT/'tmp').mkdir()
print('Retained evidence: '+str(OUT),flush=True)
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def pins():return {str(p):sha(p) for p in sorted([*SRC.rglob('*.c'),*SRC.rglob('*.h'),*HERE.glob('*')]) if p.is_file()}
before=pins()
for p in HERE.glob('*'):
 if p.is_file():(OUT/p.name).write_bytes(p.read_bytes())
(OUT/'source-before.json').write_text(json.dumps(before,indent=2)+'\n')
main=(SRC/'main.c').read_text()
# An intentionally defective reference removes only this repair from the selected
# current main. Do not compare to a stale cached executable or historic director.
# Runtime v4 already owns startup failure status and common return priority.
# Reverse only this rebased capture/graph repair, never the runtime contract.
replacements=[('                if (cap_v) fclose(cap_v);\n'
  '                if (cap_a) fclose(cap_a);\n'
  '                exit_status = 1;\n'
  '                goto shutdown;                    /* the world and its monster graphs may already be live '
  '*/',
  '                return 1;'),
 ('shutdown:\n    popup_free(&v);\n    rnd_free(v.rnd);', '    popup_free(&v);\n    rnd_free(v.rnd);'),
 ('    for (int k = 0; k < MONSTERS; k++) {\n'
  '        if (v.mon[k].have_graph) {\n'
  '            moves_stop(&v.mon[k].run);\n'
  '            moves_free(&v.mon[k].graph);\n'
  '            v.mon[k].have_graph = 0;\n'
  '        }\n'
  '        if (v.mon[k].have_ag) {                     /* independent: a move graph replacement can be '
  'absent */\n'
  '            dir_graph_free(&v.mon[k].ag);\n'
  '            v.mon[k].have_ag = 0;\n'
  '        }\n'
  '    }\n',
  '    for (int k = 0; k < MONSTERS; k++)\n'
  '        if (v.mon[k].have_graph) {\n'
  '            moves_stop(&v.mon[k].run);\n'
  '            moves_free(&v.mon[k].graph);\n'
  '        }\n')]
reference=main
for fixed,old in replacements:
 assert reference.count(fixed)==1,'Unexpected cleanup shape; update this source regression deliberately'
 reference=reference.replace(fixed,old,1)
(OUT/'reference-main.c').write_text(reference)
if os.environ.get('FINAL_AI_BASELINE_MAIN'):
 assert reference==Path(os.environ['FINAL_AI_BASELINE_MAIN']).read_text(),'Generated reference differs from supplied baseline'

def between(s,a,b):
 assert s.count(a)==1,(a,s.count(a));start=s.index(a)+len(a);end=s.index(b,start);return s[start:end]
def brace_block(s,start):
 # Selected snippets have no brace literals/comments. Explicitly reject changes.
 p=s.index('{',start);depth=0
 for q in range(p,len(s)):
  if s[q]=='{':depth+=1
  elif s[q]=='}':
   depth-=1
   if not depth:return s[start:q+1]
 raise AssertionError('unbalanced selected block')
def extracts(s):
 tail=s[s.index('    popup_free(&v);\n    rnd_free(v.rnd);',s.index('static int viewer_main')):]
 tail=tail[:tail.index('\n}\n\nint main')]
 if '\nshutdown:\n' in s:tail='shutdown:\n'+tail
 loop=between(tail,'    rnd_free(v.rnd);\n','    if (v.have_scene)')
 w=s[s.index('static void world_release(Viewer *v)'):];w=w[:w.index('\n#endif')]
 world=between(w,'    huds_close(v);\n','    block_managers_free(v);')
 m=s[s.index('static int start_moves(Mon *mo)'):]
 absent=between(m,'    mo->nick = mo->monster >= 0 && !_stricmp(v->monsters[mo->monster], "Nick");   /* the setup\'s (0x2F5D80) */\n','    if (moves_start(')
 failure=brace_block(s,s.index('            if (!cap_v || !cap_a)'))
 completion=brace_block(s,s.index('            if (cap_step == cap_to)'))
 return dict(final_loop=loop,world_loop=world,absent_prefix=absent,capture_failure=failure,capture_completion=completion,tail=tail)
def generate(x):
 return '''static void extracted_final_loop(Viewer *vp) {
#define v (*vp)
'''+x['final_loop']+'''#undef v
}
static void extracted_world_loop(Viewer *v) {
'''+x['world_loop']+'''}
static int extracted_absent_prefix(Mon *mo) {
    Viewer *v=mo->v;
'''+x['absent_prefix']+'''    return 99;
}
static int extracted_shutdown(Viewer *vp,int capture_case,FILE *cap_v,FILE *cap_a) {
    Window w={.alive=1}; int exit_status=initial_exit_status; (void)exit_status;
#define v (*vp)
#define fclose observed_fclose
    if(capture_case) {const char *c="fixture";int at=0;
'''+x['capture_failure']+'''
        post_fail_reached++;
    }
#undef fclose
'''+x['tail']+'''
#undef v
}
static int extracted_completed_capture(Viewer *vp,FILE *cap_v,FILE *cap_a) {
    Window w={.alive=1}; int exit_status=0; (void)exit_status;
    long cap_step=2, cap_to=2, cap_from=0;
#define v (*vp)
#define fclose observed_fclose
    do {
'''+x['capture_completion']+'''
    } while(0);
#undef fclose
'''+x['tail']+'''
#undef v
}
'''
flags=['-O2'] if MODE=='normal' else ['-O1','-g1','-fsanitize=address,undefined','-fno-omit-frame-pointer']
env=dict(os.environ,TMPDIR=str(OUT/'tmp'),ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
receipts=[]
def limits():resource.setrlimit(resource.RLIMIT_CORE,(0,0));resource.setrlimit(resource.RLIMIT_CPU,(45,45))
def run(cmd,label):
 start=time.monotonic()
 with (OUT/(label+'.out')).open('x') as out,(OUT/(label+'.err')).open('x') as err:
  r=subprocess.run(list(map(str,cmd)),cwd=OUT,env=env,stdout=out,stderr=err,timeout=90,preexec_fn=limits)
 receipts.append(dict(label=label,command=list(map(str,cmd)),status=r.returncode,seconds=time.monotonic()-start,stdout_sha256=sha(OUT/(label+'.out')),stderr_sha256=sha(OUT/(label+'.err'))))
 (OUT/'receipts.json').write_text(json.dumps(receipts,indent=2)+'\n')
 assert r.returncode==0,(label,r.returncode,str(OUT/(label+'.err')))
 return (OUT/(label+'.out')).read_text()
cc=[os.environ.get('CC','cc'),'-pipe','-std=gnu11','-D_DEFAULT_SOURCE','-D_FILE_OFFSET_BITS=64','-Wall','-Wextra',*flags,'-ffunction-sections','-fdata-sections','-I'+str(SRC),'-I'+str(HERE),'-I'+str(OUT)]
for unit,macros in [('director',['-Dmalloc=dir_malloc','-Dfree=dir_free']),('moves',['-Dmalloc=move_malloc','-Dcalloc=move_calloc','-Dfree=move_free'])]:
 run([*cc,'-include',HERE/'tracked.h',*macros,'-c',SRC/(unit+'.c'),'-o',OUT/(unit+'.o')],'build-'+unit)
# bdp_name and any live math dependencies come from current production units.
for unit in ('bundle','wrand'):
 run([*cc,'-c',SRC/(unit+'.c'),'-o',OUT/(unit+'.o')],'build-'+unit)
results={}
for variant,s in [('reference',reference),('candidate',main)]:
 x=extracts(s);(OUT/(variant+'-excerpts.json')).write_text(json.dumps(x,indent=2)+'\n')
 (OUT/'excerpts.inc').write_text(generate(x));(OUT/(variant+'-excerpts.inc')).write_text(generate(x))
 run([*cc,'-DCANDIDATE='+str(int(variant=='candidate')),OUT/'probe.c',*[OUT/(u+'.o') for u in ('director','moves','bundle','wrand')],'-Wl,--gc-sections','-lm','-o',OUT/variant],'build-'+variant)
 results[variant]=run([OUT/variant],'run-'+variant)
assert before==pins(),'Selected source/test package changed during run'
(OUT/'source-after.json').write_text(json.dumps(before,indent=2)+'\n')
(OUT/'result.json').write_text(json.dumps(dict(passed=True,mode=MODE,source_unchanged=True,reference_sha256=sha(OUT/'reference-main.c'),current_main_sha256=sha(SRC/'main.c'),tracked_scope='director.c and moves.c graph allocations only; non-graph subsystem spies; LeakSanitizer disabled',results={k:v.splitlines() for k,v in results.items()}),indent=2)+'\n')
print(results['reference'].splitlines()[2]);print(results['candidate'].splitlines()[-1]);print('PASS '+str(OUT),flush=True)
