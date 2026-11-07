#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
static int calls, fail_at, live;
static void *tmalloc(size_t n) { if (++calls == fail_at) return NULL; void *p=malloc(n); if(p) live++; return p; }
static void *tcalloc(size_t n,size_t z) { if (++calls == fail_at) return NULL; void *p=calloc(n,z); if(p) live++; return p; }
static void tfree(void *p) { if(p) { live--; free(p); } }
#define malloc tmalloc
#define calloc tcalloc
#define free tfree
#include "tree.c"
#include "anim.c"
#define rd32 cloth_rd32
#define rd16 cloth_rd16
#define rdf cloth_rdf
#include "cloth.c"
#undef rd32
#undef rd16
#undef rdf
#undef malloc
#undef calloc
#undef free
const char *bdp_name(const Bundle *b, uint32_t n) { (void)b; (void)n; return "node"; }
static void w(uint8_t *d, int at, uint32_t n) { for(int k=0;k<4;k++) d[at+k]=(uint8_t)(n>>(8*k)); }
static void zero(const void *p,size_t n) { const unsigned char *s=p; for(size_t k=0;k<n;k++) assert(s[k]==0); }
static void reset(int at) { assert(live==0); calls=0;fail_at=at; }
int main(void) {
 uint8_t d[512]={0}; Bundle b={0}; BdpEntry e={.data=d,.size=sizeof d,.type=ET_ANIM}; Tree t; Clip clip;
 w(d,0x20,1);w(d,0x2c,0x60);w(d,0x60,0x70);w(d,0x74,UINT32_MAX);
 reset(1);memset(&t,0xa5,sizeof t);assert(tree_read(&b,&e,NULL,&t)==-1);zero(&t,sizeof t);tree_free(&t);
 reset(0);assert(tree_read(&b,&e,NULL,&t)==0);assert(t.n==1&&t.nodes[0].parent==-1&&t.nodes[0].q[3]==1&&t.nodes[0].mirror==0);tree_free(&t);
 memset(d,0,sizeof d);w(d,0x20,2);w(d,0x2c,1);w(d,0x38,0x80);w(d,0x3c,0xa0);
 for(int i=1;i<=2;i++){reset(i);memset(&clip,0xa5,sizeof clip);assert(clip_read(&e,&clip)==-1);zero(&clip,sizeof clip);clip_free(&clip);assert(live==0);}
 reset(0);assert(clip_read(&e,&clip)==0);assert(clip.bones==1&&clip.trans[0]==d+0x80&&clip.rot[0]==d+0xa0);clip_free(&clip);
 w(d,0x20,1);w(d,0x30,0x60);w(d,0x60,0x70);w(d,0x70,0);
 reset(1);assert(clip_read(&e,&clip)==-1);zero(&clip,sizeof clip);clip_free(&clip);
 reset(0);assert(clip_read(&e,&clip)==0);assert(clip.bones==1&&clip.trans[0]==d+0x70&&!clip.rot);clip_free(&clip);
 w(d,0x20,99);reset(0);assert(clip_read(&e,&clip)==-1);zero(&clip,sizeof clip);
 memset(d,0,sizeof d);w(d,0x34,1);w(d,0x38,0x40);w(d,0xc0,1);w(d,0xc8,0x100);w(d,0x100,0x3f800000);d[0x134]=7;
 ClothParticle *particles;
 reset(1);particles=(void *)(uintptr_t)1;assert(cloth_particles(&e,&particles)==0&&!particles);
 reset(0);assert(cloth_particles(&e,&particles)==1&&particles[0].pos[0]==1&&particles[0].nbones==4);tfree(particles);
 w(d,0xc0,0);reset(0);assert(cloth_particles(&e,&particles)==0&&particles);tfree(particles);
 w(d,0x34,2);w(d,0xc0,UINT32_MAX);w(d,0x14c,1);reset(0);particles=(void *)(uintptr_t)1;
 assert(cloth_particles(&e,&particles)==0&&!particles&&calls==0&&live==0);
 uint8_t mesh[256]={0}, data2[128]={0};Part part={.n=3,.first=5};Cloth c={.nparts=1,.parts=&part};ClothMeshPart out;
 w(mesh,0,1);w(mesh,0x5c,0x60);w(mesh,0x60,0x70);w(mesh,0x80,3);w(mesh,0x88,0);
 w(mesh,0x90,3);w(mesh,0x98,0x100);w(mesh,0xa0,16);w(mesh,0xa4,60);
 data2[2]=1;data2[4]=2;w(data2,60,0x3f800000);data2[64]=0;data2[65]=0x10;
 for(int i=1;i<=3;i++){reset(i);memset(&out,0xa5,sizeof out);assert(cloth_mesh_part(&c,0,mesh,sizeof mesh,data2,sizeof data2,&out)==-2);zero(&out,sizeof out);cloth_mesh_part_free(&out);assert(live==0);}
 reset(0);assert(cloth_mesh_part(&c,0,mesh,sizeof mesh,data2,sizeof data2,&out)==0);
 assert(out.nverts==3&&out.ntris==1&&out.particle[0]==5&&out.particle[2]==7&&out.uv[0]==1&&out.tris[0]==0&&out.tris[1]==1&&out.tris[2]==2);cloth_mesh_part_free(&out);
 reset(0);data2[0]=3;assert(cloth_mesh_part(&c,0,mesh,sizeof mesh,data2,sizeof data2,&out)==-1);zero(&out,sizeof out);assert(live==0);
 reset(0);data2[0]=0;w(mesh,0xa4,48);assert(cloth_mesh_part(&c,0,mesh,sizeof mesh,data2,sizeof data2,&out)==-1);zero(&out,sizeof out);assert(live==0);
 reset(0);w(mesh,0x80,2);assert(cloth_mesh_part(&c,0,mesh,sizeof mesh,data2,sizeof data2,&out)==-1);zero(&out,sizeof out);
 puts("PASS: reader valid results, zero-count ownership, mapping and truncated-UV rejection; zero live allocations");
 puts("PASS: all 8 allocation failures (tree 1, clips 3, particles 1, cloth mesh 3); zero output and safe cleanup");
 return 0;
}
