#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "city.h"
static unsigned allocations,frees,fail_next;static size_t live_bytes;static int debug_flag;
static struct {void*p;size_t n;} blocks[16];
static void *xrealloc(void*p,size_t n){allocations++;if(fail_next){fail_next--;return NULL;}unsigned k=0;for(;k<16;k++)if(blocks[k].p==p)break;assert(k<16);void*q=realloc(p,n);if(q){live_bytes+=n-blocks[k].n;blocks[k].p=q;blocks[k].n=n;}return q;}
static void*xmalloc(size_t n){return xrealloc(NULL,n);}
static void xfree(void*p){if(p){for(unsigned k=0;k<16;k++)if(blocks[k].p==p){live_bytes-=blocks[k].n;blocks[k].p=NULL;blocks[k].n=0;frees++;break;}}free(p);}
static char *test_getenv(const char*n){(void)n;return debug_flag?"1":NULL;}
static char diagnostic[160];static int test_printf(const char*f,...){va_list ap;va_start(ap,f);int n=vsnprintf(diagnostic,sizeof diagnostic,f,ap);va_end(ap);return n;}
#define malloc xmalloc
#define realloc xrealloc
#define free xfree
#define getenv test_getenv
#define printf test_printf
struct Cloth {int n,nodes;float world[16],pos[360],nrm[360];};
int cloth_nodes(const Cloth*c){return c->nodes;}int cloth_count(const Cloth*c){return c->n;}
void cloth_set_world(Cloth*c,const float w[16]){memcpy(c->world,w,64);}
void cloth_reset(Cloth*c,const float(*s)[16]){(void)c;(void)s;}
void cloth_step(Cloth*c,float dt,const float(*s)[16],const float w[3]){(void)c;(void)dt;(void)s;(void)w;}
void cloth_flag_wind(const ClothWind*w,float t,float out[3]){(void)w;(void)t;out[0]=out[1]=out[2]=0;}
int cloth_collide_plane(Cloth*c,const float n[3],float d,const float v[3]){(void)c;(void)n;(void)d;(void)v;return 0;}
void water_free(Water*w){assert(!w);}void mesh_free(Mesh*m){(void)m;}void cloth_free(Cloth*c){(void)c;}
void tree_free(Tree*t){(void)t;}void clip_free(Clip*c){(void)c;}void bdp_free(Bundle*b){(void)b;}void people_free(People*p){(void)p;}void building_free(Building*b){(void)b;}

