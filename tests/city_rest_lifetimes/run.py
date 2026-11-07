#!/usr/bin/env python3
import os,sys,hashlib,json,subprocess,tempfile,argparse
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--sanitize',action='store_true');args=parser.parse_args()
root=Path(os.environ.get('RAMPAGE_ROOT',Path(__file__).resolve().parents[2]))
source=Path(os.environ.get('CITY_SOURCE',root/'src/city.c'));s=source.read_text();before=hashlib.sha256(source.read_bytes()).hexdigest();runner_hash=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
assert s.count('        sb->rest = malloc(')+s.count('        Mat4 *rest = malloc(')==1,'Expected unique rest allocation block'
start=s.index('        sb->rest = malloc(') if '        sb->rest = malloc(' in s else s.index('        Mat4 *rest = malloc(')
end=s.index('        facade_passes(s, sb);',start)
block=s[start:end];base=Path(os.environ.get('TMPDIR',root/'.test-results'));base.mkdir(parents=True,exist_ok=True);work=Path(tempfile.mkdtemp(prefix='city-rest-',dir=base));print('Retained artifacts:',work,flush=True)
c='''#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef struct { float m[16]; } Mat4;
typedef struct { Mat4 base; } Instance;
typedef struct { Instance *instances; } Scene;
typedef struct { int first,last; Mat4 *rest; } SceneBuilding;
static int fail,live;static size_t bytes;
static void *alloc(size_t n){if(fail)return NULL;void*p=malloc(n);assert(p);live++;bytes=n;return p;}
#define malloc alloc
static int construct(Scene*s,SceneBuilding*sb){
'''+block+'''return 0;}
#undef malloc
int main(void){Instance in[64];for(int i=0;i<64;i++)for(int j=0;j<16;j++)in[i].base.m[j]=(float)(i*17+j);Scene s={in};for(int k=0;k<2000;k++){int first=k%31,last=first+k%32;SceneBuilding b={first,last,NULL};fail=1;assert(construct(&s,&b)==-1);assert(!b.rest&&!live);fail=0;assert(!construct(&s,&b));assert(b.rest&&live==1&&bytes==sizeof(Mat4)*(size_t)(last-first+1));for(int i=first;i<last;i++)assert(!memcmp(&b.rest[i-first],&in[i].base,sizeof(Mat4)));free(b.rest);live--;assert(!live);}puts("PASS 2000 failures, 2000 byte-equivalent successes, zero live allocations");}
'''
(work/'probe.c').write_text(c)
flags=['-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer'] if args.sanitize else ['-O2']
cmd=[os.environ.get('CC','cc'),'-std=c99','-Wall','-Wextra','-Werror',*flags,str(work/'probe.c'),'-o',str(work/'probe')]
env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0:halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'}
r=subprocess.run(cmd,capture_output=True,text=True,env=env);(work/'build.log').write_text(r.stdout+r.stderr);assert not r.returncode
r=subprocess.run([str(work/'probe')],capture_output=True,text=True,env=env);(work/'run.log').write_text(r.stdout+r.stderr)
assert hashlib.sha256(source.read_bytes()).hexdigest()==before,'Source changed during tests'
(work/'receipt.json').write_text(json.dumps(dict(source=str(source.resolve()),sha256=before,runner_sha256=runner_hash,command=cmd,status=r.returncode,extract_start_line=s[:start].count('\n')+1,extract=block),indent=2));print(r.stdout+r.stderr);print('Retained artifacts:',work);sys.exit(r.returncode)
