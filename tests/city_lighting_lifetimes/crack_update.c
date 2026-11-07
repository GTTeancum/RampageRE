#define main construction_main
#include "regression.c"
#undef main
#include "ground_cracks.inc"
int main(int argc,char **argv) {
 (void)argv;start(0);Scene s=setup(0);
 if(argc>1){assert(!copy_mesh(&s.meshes[0].mesh,&s.meshes[2].mesh));s.nmeshes=3;s.items[0].mesh=2;s.buildings[0].nlit_items=1;s.buildings[0].lit_item[0]=0;s.buildings[0].lit_src[0]=0;calls=0;fail_at=5;overlay(&s,s.buildings);fail_at=0;}
 else assert(!build_tail(&s));
 SceneBuilding *b=s.buildings; b->b.vert=test_calloc(1,sizeof *b->b.vert);b->crack_tint[0]=0.2f;b->crack_tint[1]=0.5f;b->crack_tint[2]=0.9f;
 MeshPart *p=&s.meshes[s.items[b->gf_item].mesh].mesh.parts[b->gf_part];assert(p->ntris==0);
 FILE *f=argc>1?fopen("baseline_crack_fault.bin","wb"):dump_open("crack",0);assert(f);
 for(int phase=0;phase<5;phase++) {
 b->b.ground_hit=phase==1||phase==2;
 for(int face=0;face<3;face++)for(int col=0;col<3;col++)b->b.vert[0][face][col].level=(phase==3?2:0);
 ground_cracks(&s,b);
 assert(p->ntris==(phase==0||phase==4?0:1));
 if(phase==1)assert(b->gf_vert[0][2]==102);
 if(phase==2)assert(b->gf_vert[0][2]==204);
 dump_mesh(f,&s.meshes[s.items[b->gf_item].mesh].mesh);fwrite(b->gf_vert,3*p->nverts,1,f);
 }
 assert(!fclose(f));test_free(b->b.vert);b->b.vert=NULL;scene_free(&s);assert(!live&&!bytes&&made==released);
 printf("PASS 5 actual ground_cracks phases, %zu allocations freed, zero live bytes\n",released);return 0;
}
