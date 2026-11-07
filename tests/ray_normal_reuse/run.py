import pathlib, hashlib, json, subprocess, os, re, tempfile, sys
HERE=pathlib.Path(__file__).resolve().parent
R=pathlib.Path(os.environ.get('SOURCE_DIR', str(HERE.parents[1]/'src'))).resolve()
TMP=pathlib.Path(os.environ.get('TMPDIR',str(HERE/'results'))).resolve();TMP.mkdir(parents=True,exist_ok=True)
W=pathlib.Path(tempfile.mkdtemp(prefix='ray-normal-',dir=TMP))
s=(R/'collide.c').read_text()
old='''            if (l2 >= 1e-6f)
                for (int k = 0; k < 3; k++)
                    ln[k] = s[k] / sqrtf(l2);
            else'''
new='''            if (l2 >= 1e-6f) {
                float len = sqrtf(l2);
                for (int k = 0; k < 3; k++)
                    ln[k] = s[k] / len;
            } else'''
assert s.count(new)==1 and old not in s, "Expected integrated local sqrt reuse block exactly once"
candidate=s
s=s.replace(new,old)
def extract(name):
    m=list(re.finditer(r'^(?:static )?(?:float|void|int) '+name+r'\([^;]*?\)\s*\{',s,re.M)); assert len(m)==1,name
    start=m[0].start(); pos=s.index('{',start); depth=1; end=pos+1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]
helpers='\n'.join(extract(n) for n in ['dot3','to_local','to_world_dir','point_local','collide_matrix'])
ray=extract('collide_obj_ray')
probe='''#include "collide.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define BIG 1.7e38f
static unsigned calls;
static float counted_sqrtf(float x) { ++calls; return sqrtf(x); }
'''+helpers+'\n#define sqrtf counted_sqrtf\n'+ray.replace('collide_obj_ray(', 'baseline_ray(')+'\n'+ray.replace(old,new).replace('collide_obj_ray(', 'candidate_ray(')+'\n#undef sqrtf\n'+(HERE/'cases.c').read_text()
assert ray.replace(old,new) in candidate
(W/'probe.c').write_text(probe)

pins={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [R/'collide.c',R/'collide.h',HERE/'cases.c',HERE/'run.py']}
(W/'manifest.json').write_text(json.dumps(pins,indent=2)+'\n')
sanitize='--sanitize' in sys.argv
flags=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'] if sanitize else ['-O2']
cmd=[os.environ.get('CC','cc'),'-pipe','-std=c11','-Wall','-Wextra','-Werror','-fno-builtin',*flags,'-I',str(R),str(W/'probe.c'),'-lm','-o',str(W/'probe')]
(W/'command.json').write_text(json.dumps(cmd,indent=2))
env=os.environ|{'TMPDIR':str(W),'ASAN_OPTIONS':os.environ.get('ASAN_OPTIONS','detect_leaks=0:halt_on_error=1'),'UBSAN_OPTIONS':'halt_on_error=1'}
with (W/'compile.log').open('w') as f:subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,check=True)
with (W/'run.log').open('w') as f:subprocess.run([str(W/'probe')],env=env,stdout=f,stderr=subprocess.STDOUT,check=True)
assert all(hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()==h for p,h in pins.items())
print((W/'run.log').read_text(),end='');print('Retained:',W)
