#include <strings.h>
#include <ctype.h>
#define _stricmp strcasecmp
#define main roots_main
#include "storage.c"
#undef main
static int tree_bad,clip_bad,moving=1;
const char *bdp_name(const Bundle*b,uint32_t i){assert(i<b->nnames);return b->names[i];}
int tree_read(const Bundle*b,const BdpEntry*e,const BdpEntry*r,Tree*t){(void)b;(void)e;(void)r;memset(t,0,sizeof *t);if(tree_bad)return -1;t->nodes=calloc(1,sizeof *t->nodes);if(!t->nodes)return -1;t->n=1;t->nodes[0].name="part";return 0;}
void tree_free(Tree*t){free(t->nodes);memset(t,0,sizeof *t);}
int clip_read(const BdpEntry*e,Clip*c){(void)e;memset(c,0,sizeof *c);if(clip_bad)return -1;c->trans=calloc(1,sizeof *c->trans);if(!c->trans)return -1;c->bones=1;return 0;}
void clip_free(Clip*c){free(c->trans);free(c->rot);memset(c,0,sizeof *c);}
int clip_moves(const Clip*c){(void)c;return moving;}
#include "models.inc"
static void extended_tidy(Scene*s){for(uint32_t i=0;i<s->ninstances;i++){tree_free(&s->instances[i].tree);clip_free(&s->instances[i].clip);free(s->instances[i].node);}free(s->looks);s->looks=NULL;tidy(s);}
static int run_model(int failure,int vehicle,int existing,int tree_error,int clip_error){
 reset();tree_bad=tree_error;clip_bad=clip_error;
 const char*names[]={"model|part","model"};uint8_t raw[0x44]={0};raw[0x20]=1;raw[0x28]=1;
 BdpEntry entries[4]={{.type=ET_MESH,.name=0},{.type=ET_TREE,.name=1},{.type=ET_ANIM,.name=1,.data=raw,.size=sizeof raw},{.type=ET_MESH,.name=0}};
 char keys[4][64]={"model","","","model"};City city={0};city.bundle=(Bundle){.entries=entries,.nentries=4,.names=names,.nnames=2};city.model_key=keys;
 Scene s={0};int32_t cache[4]={-1,-1,-1,-1};s.city=&city.bundle;s.mesh_of_entry=cache;
 struct PersonLook snapshot={0};void*owner=NULL;
 if(existing){s.looks=calloc(1,sizeof *s.looks);s.nlooks=1;s.looks[0].who=55;strcpy(s.looks[0].anim,"other");s.looks[0].inst=-1;snapshot=s.looks[0];owner=s.looks;}
 int start=calls;fail_at=failure?calls+failure:0;
 int result=vehicle?vehicle_look(&city,&s,7,"MODEL.PWK"):place_model(&city,&s,"MODEL.PWK",mat_identity(),-1);
 int allocations=calls-start;
 if(failure||tree_error||clip_error){assert(result==-2&&s.construction_failed);if(existing){assert(s.looks==owner&&s.nlooks==1&&!memcmp(&snapshot,s.looks,sizeof snapshot));}int before=calls;assert(place_model(&city,&s,"model",mat_identity(),-1)==-2);assert(vehicle_look(&city,&s,7,"MODEL.PWK")==-2);assert(calls==before);}
 else{assert(result==0&&s.ninstances==1&&s.nmeshes==2&&s.nitems==2);assert(s.instances[0].tree.n==1);assert(s.instances[0].has_clip==moving);assert(s.items[0].node==0&&s.items[1].node==0);if(vehicle){assert(s.nlooks==1+existing&&s.instances[0].dynamic);int before=calls;assert(vehicle_look(&city,&s,7,"MODEL.PWK")==0&&calls==before);}}
 extended_tidy(&s);return allocations;
}
int main(void){roots_main();for(int vehicle=0;vehicle<2;vehicle++)for(int existing=0;existing<2;existing++){if(!vehicle&&existing)continue;int total=run_model(0,vehicle,existing,0,0);for(int f=1;f<=total;f++)run_model(f,vehicle,existing,0,0);printf("constructor vehicle=%d existing=%d failed all %d allocation points; zero tracked owners\n",vehicle,existing,total);}run_model(0,0,0,1,0);run_model(0,0,0,0,1);moving=0;run_model(0,0,0,0,0);puts("extended place_model / vehicle_look PASS with reader stubs");return 0;}
