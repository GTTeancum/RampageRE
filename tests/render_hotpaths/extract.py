#!/usr/bin/env python3
"""Extract current complete renderer functions; replace only GL/allocator boundaries."""
import hashlib,json,re,sys
from pathlib import Path
R=Path(sys.argv[1]).resolve();O=Path(sys.argv[2]).resolve();T=Path(__file__).resolve().parent
spans=[]
def take(file,name,pattern):
 s=(R/'src'/file).read_text();ms=list(re.finditer(pattern,s,re.M|re.S))
 if len(ms)!=1:raise SystemExit(f'{file} {name}: expected one source span, found {len(ms)}')
 m=ms[0];code=m.group();line=s.count('\n',0,m.start())+1
 spans.append(dict(file=file,name=name,line=line,sha256=hashlib.sha256(code.encode()).hexdigest()))
 return '#line %d %s\n%s\n'%(line,json.dumps(str(R/'src'/file)),code)
def fn(file,name):return take(file,name,rf'^(?:static )?[^\n]*\b{name}\([^;\n]*\)\n\{{.*?^\}}')
code='#include "boundary.h"\n'
code+=take('render_gl.c','renderer types',r'^typedef struct \{ uint32_t name; float reg\[4\]; GLuint id; float scale\[2\]; \} RegionTex;.*?^struct Renderer \{.*?^\};')
code+=take('render_gl.c','buffer functions',r'^typedef void \(APIENTRY \*BufGen\).*?^static struct \{ int tried; BufGen gen; BufDel del; BufBind bind; BufData data; \} g_buf;')
code+='#include "dependencies.h"\n'
for f in ['bdp_name','bdp_entry','bdp_entry_of','bdp_find_name']:code+=fn('bundle.c',f)
for f in ['material_texture','material_color','material_layer','material_env','material_damage_layer','material_flags','material_blended','material_uncut','material_clamped','material_region']:code+=fn('mesh.c',f)
code+=take('render_gl.c','rnd_create',r'^Renderer \*rnd_create\(void\) \{[^\n]*\}')
for f in ['drop_textures','drop_buffers','rnd_scene','rnd_free','draw_part','draw_water','draw_shadows']:code+=fn('render_gl.c',f)
code+='\n#undef realloc\n#undef free\n#undef fmod\n#include "regression.c"\n'
O.write_text(code)
O.with_suffix('.json').write_text(json.dumps({'sources':{f:hashlib.sha256((R/'src'/f).read_bytes()).hexdigest() for f in ['render_gl.c','mesh.c','bundle.c']},'spans':spans,'generated_sha256':hashlib.sha256(code.encode()).hexdigest()},indent=2)+'\n')
