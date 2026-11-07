#!/usr/bin/env python3
"""Current anim.c plus a narrow reference with only the historical raw trig loop restored."""
import hashlib,json,re,sys
from pathlib import Path
R=Path(sys.argv[1]).resolve();O=Path(sys.argv[2]).resolve();s=(R/'src/anim.c').read_text()
ms=list(re.finditer(r'^int clip_pose_phase\([^\n]*\)\n\{.*?^\}',s,re.M|re.S))
if len(ms)!=1:raise SystemExit('Expected one clip_pose_phase definition')
m=ms[0];body=m.group()
new='    float cosine = angle == 0 ? 0 : cosf(angle * fr), sine = angle == 0 ? 0 : sinf(angle * fr);\n'
new_expr='q0 * cosine + q1 * sine'
old_expr='q0 * cosf(angle * fr) + q1 * sinf(angle * fr)'
if body.count(new)!=1 or body.count(new_expr)!=1:raise SystemExit('Review changed RawAnim trig implementation before changing the differential reference')
reference=body.replace('clip_pose_phase(const Clip *c, double f,', 'reference_pose(const Clip *c, double seconds,',1).replace(new,'').replace(new_expr,old_expr)
guard='    if (bone >= c->bones || c->duration <= 0)\n        return 0;\n'
if reference.count(guard)!=1:raise SystemExit('Review changed clip pose early-return guard')
reference=reference.replace(guard,guard+'    double f = fmod(seconds / c->duration, 1.0);\n    if (f < 0) f += 1;\n',1)
code='#include "anim_boundary.h"\n#line 1 '+json.dumps(str(R/'src/anim.c'))+'\n'+s+'\n'+reference+'\n#undef fmod\n#undef cosf\n#undef sinf\n#include "anim_regression.c"\n'
O.write_text(code);O.with_suffix('.json').write_text(json.dumps({'source_sha256':hashlib.sha256(s.encode()).hexdigest(),'clip_pose_sha256':hashlib.sha256(body.encode()).hexdigest(),'reference_change':{'removed_declaration':new,'new_expression':new_expr,'reference_expression':old_expr},'generated_sha256':hashlib.sha256(code.encode()).hexdigest()},indent=2)+'\n')
