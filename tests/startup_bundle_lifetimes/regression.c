/* Stub loader outcomes, actual extracted startup control flow. The stubs
 * enforce bdp_load's success-only input transfer contract. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "data.h"
#define PROFILE_CITIES 10
static struct {
 Vol vol;Bundle gui_world,gui_common,wii_shelldata;int have_gui;
 Book wii_shellbook;struct{int n;}wii_roster;const char*wii_cities[PROFILE_CITIES];int nwii_cities;
} v;
static void*live[16];static int allocations,raw_mode[3],book_bad;
static void *getmem(size_t n){void*p=malloc(n);assert(p);for(int i=0;i<16;i++)if(!live[i]){live[i]=p;allocations++;return p;}abort();}
static void putmem(void*p){if(!p)return;for(int i=0;i<16;i++)if(live[i]==p){live[i]=NULL;allocations--;free(p);return;}assert(!"double/unowned free");}
uint8_t *vol_read(const Vol*vol,const char*name,uint32_t*size){(void)vol;int i=!strcmp(name,"WorldGui.bdp")?0:!strcmp(name,"CommonGUI.bdp")?1:2;if(!raw_mode[i])return NULL;*size=96;uint8_t*p=getmem(*size);memset(p,0,*size);p[0]=(uint8_t)i;return p;}
int bdp_load(Bundle*b,uint8_t*f,uint32_t n){int i=f[0];memset(b,0,sizeof*b);if(raw_mode[i]==2)return -1;b->file=f;b->file_size=n;b->names=getmem(sizeof*b->names);return 0;}
void bdp_free(Bundle*b){putmem(b->file);putmem((void*)b->names);memset(b,0,sizeof*b);}
int book_open(Book*b,const Bundle*data){(void)b;(void)data;return book_bad?-1:0;}
Group book_group(const Book*b,const char*n){(void)b;(void)n;return (Group){0};}
Group group_sub(Group g,const char*n){(void)n;return g;}
const char *group_str(Group g,const char*n){(void)g;(void)n;return NULL;}
static void reveal_roster_load(void*roster,Group g){(void)roster;(void)g;}
#define free putmem
#include "startup_branches.c"
#undef free
int main(int argc,char**argv){
 int wii_only=argc>1&&!strcmp(argv[1],"--wii-only");
 if(!wii_only)for(int a=0;a<3;a++)for(int b=0;b<3;b++){
  memset(&v,0,sizeof v);raw_mode[0]=a;raw_mode[1]=b;startup_gui();assert(v.have_gui==(a==1&&b==1));assert(allocations==(v.have_gui?4:0));bdp_free(&v.gui_world);bdp_free(&v.gui_common);assert(!allocations);
 }
 for(int present=0;present<2;present++)for(int raw=0;raw<3;raw++)for(int bad=0;bad<2;bad++){
  memset(&v,0,sizeof v);raw_mode[2]=raw;book_bad=bad;startup_wii(present);assert(allocations==((present&&raw==1&&!bad)?2:0));bdp_free(&v.wii_shelldata);assert(!allocations);
 }
 puts("PASS: 9 GUI and 12 Wii startup ownership branches; later destructors safe, zero live allocations.");return 0;
}
