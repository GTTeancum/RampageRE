#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <stddef.h>
#include <math.h>
#define RAMPAGE_PLAT_H
#define APIENTRY
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#include "render_gl.h"
#include "texpack.h"
typedef unsigned GLuint;typedef unsigned GLenum;typedef int GLsizei;typedef int GLint;typedef float GLfloat;
enum {GL_VERSION=1,GL_EXTENSIONS,GL_TEXTURE_2D,GL_RGBA,GL_UNSIGNED_BYTE,GL_TEXTURE_MIN_FILTER,GL_TEXTURE_MAG_FILTER,GL_LINEAR,GL_LINEAR_MIPMAP_LINEAR,GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T,GL_REPEAT,GL_CLAMP_TO_EDGE,GL_ARRAY_BUFFER,GL_ELEMENT_ARRAY_BUFFER,GL_STATIC_DRAW,GL_DYNAMIC_DRAW};
struct Allocation {void *p;size_t n;};static struct Allocation ledger[512];
static size_t live_bytes,peak_bytes,live_owners,alloc_calls,fail_at,alloc_failures;
static int count_allocation(void){++alloc_calls;if(fail_at&&alloc_calls==fail_at){++alloc_failures;return 0;}return 1;}
static void *keep(void *p,size_t n){assert(p);for(int i=0;i<512;i++)if(!ledger[i].p){ledger[i]=(struct Allocation){p,n};live_bytes+=n;live_owners++;if(live_bytes>peak_bytes)peak_bytes=live_bytes;return p;}abort();}
static void *a_malloc(size_t n){if(!count_allocation())return NULL;assert(n<8*1024*1024);return keep(malloc(n?n:1),n);}
static void *a_calloc(size_t n,size_t s){assert(!n||s<=SIZE_MAX/n);void *p=a_malloc(n*s);if(p)memset(p,0,n*s);return p;}
static void a_free(void *p){if(!p)return;for(int i=0;i<512;i++)if(ledger[i].p==p){live_bytes-=ledger[i].n;live_owners--;ledger[i]=(struct Allocation){0};free(p);return;}assert(!"free of unowned CPU allocation");}
static void *a_realloc(void *p,size_t n){if(!p)return a_malloc(n);if(!count_allocation())return NULL;for(int i=0;i<512;i++)if(ledger[i].p==p){void *q=malloc(n?n:1);assert(q);memcpy(q,p,n<ledger[i].n?n:ledger[i].n);a_free(p);return keep(q,n);}assert(0);return NULL;}
static void fail_next(size_t nth){fail_at=alloc_calls+nth;}
static uint64_t trace_hash=1469598103934665603ULL;static size_t trace_calls;
static FILE *trace_file;
static void hash_bytes(const void *p,size_t n){if(trace_file)assert(fwrite(p,1,n,trace_file)==n);const unsigned char *b=p;for(size_t i=0;i<n;i++){trace_hash^=b[i];trace_hash*=1099511628211ULL;}}
static void tr(unsigned tag,uint64_t a,uint64_t b,uint64_t c){uint64_t args[4]={tag,a,b,c};hash_bytes(args,sizeof args);trace_calls++;}
static unsigned next_texture=1,next_buffer=1;static unsigned char texture_live[65536],buffer_live[65536];
static unsigned texture_count,buffer_count,invalid_gl,tex_failure,buf_failure,bind_texture,bind_array,bind_element;
static unsigned image_calls,data_calls,finish_calls,pack_mode,last_min_filter;
static void glGenTextures(GLsizei n,GLuint *id){tr(1,n,0,0);assert(n==1);if(tex_failure){if(tex_failure==1)*id=0;tex_failure=0;return;}assert(next_texture<65536);*id=next_texture++;texture_live[*id]=1;texture_count++;}
static void glDeleteTextures(GLsizei n,const GLuint *id){for(int i=0;i<n;i++){tr(2,id[i],0,0);if(!id[i])continue;assert(id[i]<65536&&texture_live[id[i]]);texture_live[id[i]]=0;texture_count--;if(bind_texture==id[i])bind_texture=0;}}
static void glBindTexture(GLenum t,GLuint id){tr(3,t,id,0);if(!id||id>=65536||!texture_live[id])invalid_gl++;bind_texture=id;}
static void glTexParameteri(GLenum t,GLenum p,GLint v){tr(4,t,p,v);if(p==GL_TEXTURE_MIN_FILTER)last_min_filter=v;}
static void glTexParameterf(GLenum t,GLenum p,GLfloat v){uint32_t u;memcpy(&u,&v,4);tr(5,t,p,u);}
static void glTexImage2D(GLenum t,GLint level,GLint internal,GLsizei w,GLsizei h,GLint border,GLenum format,GLenum type,const void *px){tr(6,t,level,internal);tr(7,w,h,border);tr(8,format,type,bind_texture);hash_bytes(px,(size_t)w*h*4);image_calls++;}
static void glFinish(void){tr(9,0,0,0);finish_calls++;}
static const unsigned char *glGetString(GLenum n){return (const unsigned char *)(n==GL_VERSION?"2.1":"GL_EXT_texture_filter_anisotropic GL_ARB_vertex_buffer_object");}
static void glGetFloatv(GLenum p,GLfloat *v){(void)p;*v=8;}
static void fake_buf_gen(GLsizei n,GLuint *id){tr(10,n,0,0);assert(n==2);unsigned fail=buf_failure;buf_failure=0;for(int i=0;i<n;i++){if(fail&(1u<<i))continue;assert(next_buffer<65536);id[i]=next_buffer++;buffer_live[id[i]]=1;buffer_count++;}}
static void fake_buf_del(GLsizei n,const GLuint *id){for(int i=0;i<n;i++){tr(11,id[i],0,0);if(!id[i])continue;assert(id[i]<65536&&buffer_live[id[i]]);buffer_live[id[i]]=0;buffer_count--;if(bind_array==id[i])bind_array=0;if(bind_element==id[i])bind_element=0;}}
static void fake_buf_bind(GLenum t,GLuint id){tr(12,t,id,0);if(id&&(id>=65536||!buffer_live[id]))invalid_gl++;if(t==GL_ARRAY_BUFFER)bind_array=id;else bind_element=id;}
static void fake_buf_data(GLenum t,ptrdiff_t n,const void *p,GLenum use){tr(13,t,n,use);if(!(t==GL_ARRAY_BUFFER?bind_array:bind_element))invalid_gl++;hash_bytes(p,n);data_calls++;}
static void (*plat_gl_proc(const char *n))(void){if(!strcmp(n,"glGenBuffers"))return (void(*)(void))fake_buf_gen;if(!strcmp(n,"glDeleteBuffers"))return (void(*)(void))fake_buf_del;if(!strcmp(n,"glBindBuffer"))return (void(*)(void))fake_buf_bind;if(!strcmp(n,"glBufferData"))return (void(*)(void))fake_buf_data;return NULL;}
double win_time(void){return 1;}
int tp_active(void){return pack_mode!=0;}
uint64_t tp_hash(char k,const void *p,uint32_t n,int w,int h){tr(14,k,n,w);hash_bytes(p,n);(void)h;return 42;}
int tp_replace(uint64_t hash,int mode){tr(15,hash,mode,0);return pack_mode==2;}
uint8_t *tp_pixels(uint64_t hash,int mode,int *w,int *h){tr(16,hash,mode,0);if(pack_mode!=2)return NULL;int pw=*w*2,ph=*h*2;uint8_t *p=a_malloc((size_t)pw*ph*4);if(p){memset(p,0x8a,(size_t)pw*ph*4);*w=pw;*h=ph;}return p;}
void tp_upload(uint8_t *p,int w,int h,int mode){tr(17,w,h,mode);hash_bytes(p,(size_t)w*h*4);image_calls++;}
void tp_scope(const char *scope){(void)scope;tr(18,0,0,0);}
void tp_dump(uint64_t hash,const uint8_t *rgba,int w,int h){tr(19,hash,w,h);hash_bytes(rgba,(size_t)w*h*4);}
#define try_exact_palette(r) ((void)0)
#define malloc a_malloc
#define calloc a_calloc
#define realloc a_realloc
#define free a_free

#ifdef ACTUAL_TEXTURES
#define bdp_name cache_bdp_name
#define bdp_entry cache_bdp_entry
#define bdp_entry_of cache_bdp_entry_of
#endif
