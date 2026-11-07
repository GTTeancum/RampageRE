#!/usr/bin/env python3
"""Current-source renderer cache and complete decoder extraction; no frozen implementation."""
import hashlib,json,re,sys
from pathlib import Path
R,O=map(lambda s:Path(s).resolve(),sys.argv[1:3]);S=Path(sys.argv[3]).resolve() if len(sys.argv)>3 else R/'src'
O.mkdir(parents=True,exist_ok=True);H=O/'headers';H.mkdir(exist_ok=True)
inputs={};spans=[]
def read(n):
 p=S/n if (S/n).is_file() else R/'src'/n
 b=p.read_bytes();inputs[str(p)]=hashlib.sha256(b).hexdigest();return b.decode()
for p in (R/'src').glob('*.h'):(H/p.name).write_text(read(p.name))
def take(file,name,pat):
 s=read(file); ms=list(re.finditer(pat,s,re.M|re.S));assert len(ms)==1,(file,name,len(ms))
 m=ms[0];line=s.count('\n',0,m.start())+1;code=m.group()
 spans.append(dict(file=file,name=name,line=line,sha256=hashlib.sha256(code.encode()).hexdigest()))
 return '#line %d %s\n%s\n'%(line,json.dumps(str(S/file if (S/file).is_file() else R/'src'/file)),code)
def fn(n,f='render_gl.c'):return take(f,n,rf'^(?:static )?[^\n]*\b{n}\([^;\n]*\)\n\{{.*?^\}}')
code='#include "boundary.h"\n'
code+=take('render_gl.c','types',r'^typedef struct \{ uint32_t name; float reg\[4\]; GLuint id; float scale\[2\]; \} RegionTex;.*?^struct Renderer \{.*?^\};')
code+=take('render_gl.c','buffer state',r'^typedef void \(APIENTRY \*BufGen\).*?^static struct \{ int tried; BufGen gen; BufDel del; BufBind bind; BufData data; \} g_buf;')
code+=take('render_gl.c','texture settings',r'^#define GL_TEXTURE_MAX_ANISOTROPY.*?^static int g_aniso = .*?;')
for name in ['bdp_name','bdp_entry','bdp_entry_of']:code+=fn(name,'bundle.c')
code+='#line 1 "'+str(S/'tex.c' if (S/'tex.c').is_file() else R/'src/tex.c')+'"\n'+read('tex.c')+'\n'
code+=take('render_gl.c','rnd_create',r'^Renderer \*rnd_create\(void\) \{[^\n]*\}')
for name in ['pump_output','rnd_aniso_max','tex_aniso','upload_mips','drop_textures','buffers_ok','drop_buffers','part_buffers','rnd_scene','rnd_free','bundle_tex','texture_at','region_rect','region_texture']:code+=fn(name)
code+='\n#ifdef ACTUAL_TEXTURES\n#include "actual_regression.c"\n#else\n#include "regression.c"\n#endif\n';(O/'extracted.c').write_text(code)
(O/'manifest.json').write_text(json.dumps(dict(inputs=inputs,spans=spans,generated_sha256=hashlib.sha256(code.encode()).hexdigest()),indent=2)+'\n')
