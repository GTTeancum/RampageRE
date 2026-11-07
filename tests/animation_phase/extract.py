#!/usr/bin/env python3
from pathlib import Path
import re,sys,json,hashlib
R=Path(sys.argv[1]).resolve();O=Path(sys.argv[2]).resolve();T=Path(__file__).resolve().parent
manifest=json.loads((T/'reference_manifest.json').read_text())
for name,digest in manifest['references'].items():
 if hashlib.sha256((T/name).read_bytes()).hexdigest()!=digest:raise SystemExit('Historical reference changed: '+name)
a=(R/'src/anim.c').read_text();c=(R/'src/city.c').read_text()
def fn(s,n):
 ms=list(re.finditer(r'^(?:static )?[^\n]+\b'+n+r'\([^\n]*\)\n\{.*?^\}',s,re.M|re.S))
 if len(ms)!=1:raise SystemExit('Expected unique definition: '+n)
 return ms[0].group()
for n in ['clip_phase','clip_pose_phase','clip_pose']:fn(a,n)
p=fn(c,'pose_instance').replace('pose_instance(', 'new_instance(',1)
helpers='\n'.join(fn(c,n) for n in ['mat_identity','mat_mul','mat_trs_z','mat_tq','root_unturn','scene_monster_slot'])
code='''#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include "city.h"
static unsigned count_fmod;
static double counted_fmod(double a,double b) { ++count_fmod; return fmod(a,b); }
#define fmod counted_fmod
'''+a+'\n'+(T/'reference_clip_pose.c').read_text()+'\n'+helpers+'\n'+(T/'reference_pose_instance.c').read_text()+'\n'+p+'\n#include "regression.c"\n'
O.write_text(code)
O.with_suffix('.json').write_text(json.dumps({'current_sources':{name:hashlib.sha256((R/'src'/name).read_bytes()).hexdigest() for name in ['anim.c','city.c','city.h','data.h']},'reference_manifest':manifest,'generated_sha256':hashlib.sha256(code.encode()).hexdigest()},indent=2)+'\n')
