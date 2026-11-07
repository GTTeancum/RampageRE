from pathlib import Path
import re,hashlib,sys
root=Path(sys.argv[2]); src=(Path(sys.argv[1])/'city.c').read_text()
names=['scene_fail','skin_free','scene_vector_grow','scene_add_mesh','new_instance','push_item','find_entry','new_skin','wii_cloth_parts','flag_new']
def fn(name):
 m=re.search(r'^(?:static )?[^\n;]*\b'+name+r'\([^;]*?\)\n\{',src,re.M); assert m,name
 p=src.index('{',m.start()); depth=1;i=p+1
 while depth:
  depth += (src[i]=='{')-(src[i]=='}');i+=1
 return src[m.start():i]+'\n'
pre='''#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <limits.h>
#include <assert.h>
#include "city.h"
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
@@CITY_DECL@@
static void *live[10000]; static int calls,fail_at,nlive; static int locations[1000];
static void *a(size_t n,int line,int inject) {if(inject){locations[++calls]=line;if(calls==fail_at)return NULL;}void *p=calloc(1,n?n:1);assert(p);live[nlive++]=p;return p;}
static void f(void *p){if(!p)return;int i;for(i=0;i<nlive;i++)if(live[i]==p)break;assert(i<nlive);live[i]=live[--nlive];free(p);}
static void *r(void *p,size_t n,int line){locations[++calls]=line;if(calls==fail_at)return NULL;if(!p)return a(n,line,0);int i;for(i=0;i<nlive;i++)if(live[i]==p)break;assert(i<nlive);void *q=realloc(p,n);assert(q);live[i]=q;return q;}
#define malloc(n) a(n,__LINE__,1)
#define calloc(n,s) a((n)*(s),__LINE__,1)
#define realloc(p,n) r(p,n,__LINE__)
#define free(p) f(p)
'''
pre = pre.replace('@@CITY_DECL@@', '#include "city-status.inc"')
# exact function bodies; dependencies use actual project types but synthetic data
stubs='''
static int mapping_status,reader_fail;
const char *bdp_name(const Bundle *b,uint32_t i){(void)b;return i?"flag|mesh":"flag|cloth";}
float group_num(Group g,const char *key,float d){(void)g;(void)key;return d;}
Cloth *cloth_new(const uint8_t *d,uint32_t n){(void)d;(void)n;return a(1,0,0);}
void cloth_free(Cloth *c){f(c);}
int cloth_nearest(const Cloth *c,const float p[3]){(void)c;(void)p;return 0;}
int cloth_mesh_parts(const uint8_t *d,uint32_t n){(void)d;(void)n;return 2;}
void cloth_mesh_part_free(ClothMeshPart *p){f(p->particle);f(p->uv);f(p->tris);memset(p,0,sizeof *p);}
int cloth_mesh_part(const Cloth *c,int k,const uint8_t *d,uint32_t n,const uint8_t *d2,uint32_t n2,ClothMeshPart *p){(void)c;(void)k;(void)d;(void)n;(void)d2;(void)n2;memset(p,0,sizeof *p);if(mapping_status)return mapping_status;p->nverts=3;p->ntris=1;p->particle=a(3*sizeof(int32_t),0,0);p->uv=a(6*sizeof(float),0,0);p->tris=a(3*sizeof(uint32_t),0,0);p->tris[0]=0;p->tris[1]=1;p->tris[2]=2;return 0;}
void mesh_free(Mesh *m){for(uint32_t k=0;k<m->nparts;k++){MeshPart *p=&m->parts[k];f(p->pos);f(p->nrm);f(p->uv);f(p->tris);f(p->bones);f(p->weights);f(p->col);}f(m->parts);memset(m,0,sizeof *m);}
int mesh_read(const BdpEntry *e,Mesh *m){(void)e;memset(m,0,sizeof *m);if(reader_fail)return -1;m->nparts=2;m->parts=a(2*sizeof *m->parts,0,0);for(int k=0;k<2;k++){MeshPart *p=&m->parts[k];p->nverts=3;p->ntris=1;p->pos=a(9*sizeof(float),0,0);p->uv=a(6*sizeof(float),0,0);p->tris=a(3*sizeof(uint32_t),0,0);p->tris[1]=1;p->tris[2]=2;}return 0;}
'''
main='''
static void teardown(Scene *s){for(uint32_t i=0;i<s->nitems;i++)skin_free(s->items[i].skin);for(uint32_t i=0;i<s->nmeshes;i++)mesh_free(&s->meshes[i].mesh);f(s->meshes);f(s->items);f(s->instances);}
static int run_flag(int wii,int fail){BdpEntry e[2]={{.type=ET_CLOTH,.name=0},{.type=ET_MESH,.name=1,.wii=wii}};City c={0};c.bundle.entries=e;c.bundle.nentries=2;Scene s={0};s.city=&c.bundle;Skin *sk=NULL;calls=0;fail_at=fail;int rc=flag_new(&c,&s,"flag",(Group){0},(Mat4){0},&sk);int count=calls;if(fail||mapping_status==-2||reader_fail){assert(rc==-2&&s.construction_failed&&sk==NULL);int before=calls;assert(flag_new(&c,&s,"flag",(Group){0},(Mat4){0},&sk)==-2&&calls==before);}else if(mapping_status==-1){assert(rc==-1&&!s.construction_failed&&s.nmeshes==0&&s.nitems==0);}else{assert(rc==0&&!s.construction_failed&&s.nmeshes==1&&s.nitems==1&&sk&&sk->nverts==6);}if(fail)printf("flag wii=%d fail=%d line=%d meshes=%u items=%u\\n",wii,fail,locations[fail],s.nmeshes,s.nitems);teardown(&s);assert(nlive==0);return count;}
int main(void){assert_city_status_contract();for(int w=0;w<2;w++){int n=run_flag(w,0);for(int j=1;j<=n;j++)run_flag(w,j);printf("PASS flag wii=%d allocation points=%d\\n",w,n);}mapping_status=-1;run_flag(0,0);mapping_status=-2;run_flag(0,0);mapping_status=0;reader_fail=1;run_flag(1,0);reader_fail=0;MeshPart p[2]={{.nverts=UINT32_MAX},{.nverts=1}};Mesh m={.parts=p,.nparts=2};calls=0;fail_at=0;assert(new_skin(&m)==NULL&&nlive==0);puts("PASS mapping skip/fatal, Wii reader failure, new_skin vertex-count overflow");return 0;}
'''
(root/'flags.c').write_text(pre+stubs+'\n'+''.join(fn(n) for n in names)+main)
(root/'source.sha256').write_text(hashlib.sha256(src.encode()).hexdigest()+' city.c\n')
monster_stubs='''
static Bundle monster_mb,monster_ab;
void bdp_free(Bundle *b){(void)b;assert(0);}
static Bundle *own_bundle(const Vol *v,Scene *s,const char *f){(void)v;(void)s;return strstr(f,"_anims")?&monster_ab:&monster_mb;}
Mat4 mat_trs_z(float x,float y,float z,float d){(void)x;(void)y;(void)z;(void)d;return (Mat4){0};}
int tree_read(const Bundle *b,const BdpEntry *e,const BdpEntry *r,Tree *t){(void)b;(void)e;(void)r;memset(t,0,sizeof *t);return 0;}
int clip_read(const BdpEntry *e,Clip *c){(void)e;(void)c;assert(0);return -1;}
uint32_t cloth_particles(const BdpEntry *e,ClothParticle **out){(void)e;*out=a(sizeof **out,0,0);return 0;}
int cloth_triangle(const Cloth *c,const float p0[3],const float p1[3],const float p2[3],int out[3]){(void)c;(void)p0;(void)p1;(void)p2;(void)out;return 0;}
static void update(Scene *s,double t,int all){(void)s;(void)t;(void)all;}
'''
monster_main='''
static int run_monster(int fail){BdpEntry es[4]={{.type=ET_TREE,.extra=1},{.type=ET_CLOTH},{.type=ET_MESH},{.type=ET_MESH}};monster_mb=(Bundle){.entries=es,.nentries=4};monster_ab=(Bundle){0};City c={0};Scene s={0};calls=0;fail_at=fail;int rc=city_monster(&c,&s,"monster",-1,0,0,0);int count=calls;if(fail||mapping_status==-2||reader_fail){assert(rc==-2&&s.construction_failed);int before=calls;assert(city_monster(&c,&s,"monster",-1,0,0,0)==-2&&before==calls);}else{assert(rc==0&&!s.construction_failed&&s.nmeshes==2&&s.nitems==2&&s.nmon==1);}if(fail)printf("monster fail=%d line=%d meshes=%u items=%u\\n",fail,locations[fail],s.nmeshes,s.nitems);for(uint32_t i=0;i<s.ninstances;i++){f(s.instances[i].node);f(s.instances[i].pose);f(s.instances[i].held);}teardown(&s);assert(nlive==0);return count;}
int main(void){assert_city_status_contract();int n=run_monster(0);for(int i=1;i<=n;i++)run_monster(i);printf("PASS monster allocation points=%d\\n",n);mapping_status=-1;run_monster(0);mapping_status=-2;run_monster(0);mapping_status=0;reader_fail=1;run_monster(0);puts("PASS monster mapping fallback/fatal and mesh reader failure");return 0;}
'''
(root/'monster.c').write_text(pre+stubs+'\n'+''.join(fn(n) for n in names[:-2] if n != 'find_entry')+monster_stubs+fn('ends_with')+fn('city_monster')+main[:main.index('static int run_flag')]+monster_main)
