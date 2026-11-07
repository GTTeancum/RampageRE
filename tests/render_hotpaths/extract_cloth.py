#!/usr/bin/env python3
"""Current skinning, scratch ownership, local-coordinate conversion and teardown."""
import hashlib,json,re,sys
from pathlib import Path
R=Path(sys.argv[1]).resolve();O=Path(sys.argv[2]).resolve();spans=[]
def take(file,name,pattern):
 s=(R/'src'/file).read_text();ms=list(re.finditer(pattern,s,re.M|re.S))
 if len(ms)!=1:raise SystemExit(f'{file} {name}: expected one span, found {len(ms)}')
 m=ms[0];code=m.group();line=s.count('\n',0,m.start())+1
 spans.append(dict(file=file,name=name,line=line,sha256=hashlib.sha256(code.encode()).hexdigest()))
 return '#line %d %s\n%s\n'%(line,json.dumps(str(R/'src'/file)),code)
def fn(file,name):return take(file,name,rf'^(?:static )?[^\n]*\b{name}\([^;\n]*\)\n\{{.*?^\}}')
code='#include "cloth_boundary.h"\n'
for f in ['mat_identity','mat_mul','scene_monster_slot']:code+=fn('city.c',f)
for f in ['xf_point','xf_dir','mat_inverse_affine']:code+=fn('cloth.c',f)
code+=take('cloth.c','dot3',r'^static float dot3\([^\n]*\}');code+=fn('cloth.c','cloth_to_local')
for f in ['skin_item','skin_free','scene_free']:code+=fn('city.c',f)
code+='\n#undef printf\n#undef free\n#undef malloc\n#undef realloc\n#include "cloth_regression.c"\n'
O.write_text(code);O.with_suffix('.json').write_text(json.dumps({'sources':{f:hashlib.sha256((R/'src'/f).read_bytes()).hexdigest() for f in ['city.c','city.h','cloth.c']},'spans':spans,'generated_sha256':hashlib.sha256(code.encode()).hexdigest()},indent=2)+'\n')
