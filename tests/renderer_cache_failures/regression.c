static unsigned failure_cases,success_cases;
typedef struct {
 Bundle b;BdpEntry entries[2];const char *names[4];int32_t by[4],tex[4];
 uint8_t header[36],pixels[2048],palette[1024];
} Fixture;
static void put16(uint8_t *p,unsigned v){p[0]=v;p[1]=v>>8;}
static void put32(uint8_t *p,unsigned v){put16(p,v);put16(p+2,v>>16);}
static void fixture(Fixture *f,int wii,int format,int variation){
 memset(f,0,sizeof *f);f->names[0]="";f->names[1]="image";f->names[2]=variation==2?"_Alpha8":variation==3?"_Intensity8":"palette";f->names[3]="absent";
 f->by[0]=-1;f->by[1]=0;f->by[2]=1;f->by[3]=-1;f->tex[0]=-1;f->tex[1]=0;f->tex[2]=-1;f->tex[3]=-1;
 f->b=(Bundle){.names=f->names,.nnames=4,.entries=f->entries,.nentries=2,.by_name=f->by,.tex_by_name=f->tex,.wii=wii};
 if(wii)put32(f->header+4,format);else {f->header[4]=format;f->header[7]=variation==1?0x40:0;put32(f->header+0x20,2);}
 put16(f->header+8,16);put16(f->header+10,16);
 for(size_t i=0;i<sizeof f->pixels;i++)f->pixels[i]=(uint8_t)(i*53+variation*71+format*29);
 for(size_t i=0;i<sizeof f->palette;i++)f->palette[i]=(uint8_t)(i*43+17);
 f->entries[0]=(BdpEntry){.name=1,.type=ET_TEXTURE,.data=f->header,.size=sizeof f->header,.data2=f->pixels,.size2=sizeof f->pixels};
 f->entries[1]=(BdpEntry){.name=2,.type=ET_PALETTE,.data2=f->palette,.size2=sizeof f->palette};
}
static uint64_t bytes_hash(const void *p,size_t n){uint64_t save=trace_hash;trace_hash=1469598103934665603ULL;hash_bytes(p,n);uint64_t h=trace_hash;trace_hash=save;return h;}
static void clean(void){assert(!live_owners&&!live_bytes&&!texture_count&&!buffer_count);assert(!invalid_gl);fail_at=0;}
static void unbind_buffers(void){fake_buf_bind(GL_ARRAY_BUFFER,0);fake_buf_bind(GL_ELEMENT_ARRAY_BUFFER,0);}
static int healthy(void){
 static const int formats[][2]={{0,0},{0,2},{0,19},{1,0},{1,1},{1,3},{1,5},{1,6},{1,14}};
 for(unsigned fi=0;fi<sizeof formats/sizeof formats[0];fi++)for(int variant=0;variant<4;variant++){
  Fixture f;fixture(&f,formats[fi][0],formats[fi][1],variant);uint8_t *rgba=NULL;int w=0,h=0;
  assert(tex_decode(&f.b,&f.entries[0],&rgba,&w,&h)==0);printf("pixels wii=%d format=%d variant=%d hash=%016llx\n",formats[fi][0],formats[fi][1],variant,(unsigned long long)bytes_hash(rgba,(size_t)w*h*4));free(rgba);
  for(int hud=0;hud<2;hud++)for(int pack=0;pack<3;pack++)for(int mips=0;mips<2;mips++){
   Renderer *r=rnd_create();r->in_hud=hud;pack_mode=pack;g_mips=mips;g_aniso=8;g_aniso_max=-1;
   GLuint id=texture_at(r,&f.b,1,0);assert(id);size_t calls=alloc_calls;assert(texture_at(r,&f.b,1,0)==id&&alloc_calls==calls);
   r->in_hud_quad=1;GLuint hid=texture_at(r,&f.b,1,0);assert(hid);
#ifdef RAMPAGE_XBOX
   assert(hid!=id);
#else
   assert(hid==id);
#endif
   float reg[4]={0.125f,0.25f,0.25f,0.5f},scale[2];GLuint rid=region_texture(r,&f.b,1,reg,scale);assert(rid&&scale[0]==4&&scale[1]==2);
   calls=alloc_calls;assert(region_texture(r,&f.b,1,reg,scale)==rid&&alloc_calls==calls);
   float whole[4]={0,0,1,1};assert(!region_texture(r,&f.b,1,whole,scale));calls=alloc_calls;assert(!region_texture(r,&f.b,1,whole,scale)&&alloc_calls==calls);
   assert(!texture_at(r,&f.b,3,0));calls=alloc_calls;assert(!texture_at(r,&f.b,3,0)&&alloc_calls==calls);
   rnd_free(r);clean();success_cases++;
  }
 }
 pack_mode=g_mips=0;
 float p[36],n[36],uv[24],col[48];uint32_t tri[6]={0,1,2,2,3,0};for(int i=0;i<36;i++)p[i]=(float)i*.1f,n[i]=(float)i*.03f;for(int i=0;i<24;i++)uv[i]=(float)i*.02f;for(int i=0;i<48;i++)col[i]=(float)i*.01f;
 for(int attrs=0;attrs<8;attrs++){
  MeshPart parts[2]={{.nverts=4,.ntris=2,.pos=p,.nrm=attrs&1?n:NULL,.uv=attrs&2?uv:NULL,.col=attrs&4?col:NULL,.tris=tri}};parts[1]=parts[0];SceneMesh sm={.mesh={.parts=parts,.nparts=1}};Scene *s=calloc(1,sizeof *s);s->meshes=&sm;s->nmeshes=1;Renderer *r=rnd_create();r->scene=s;
  const PartBuf *pb=part_buffers(r,&sm,0);assert(pb&&pb->vbo&&pb->ibo);unbind_buffers();size_t calls=alloc_calls;unsigned data=data_calls;assert(part_buffers(r,&sm,0)==pb&&alloc_calls==calls&&data_calls==data);
  parts[0].rev++;assert(part_buffers(r,&sm,0)==pb);unbind_buffers();assert(data_calls==data+2);
  sm.mesh.parts=parts+1;assert(part_buffers(r,&sm,0));unbind_buffers();size_t stage=r->nstage;float *scratch=r->stage;rnd_scene(r,NULL);assert(r->stage==scratch&&r->nstage==stage&&!buffer_count);rnd_free(r);free(s);clean();success_cases++;
 }
 printf("healthy cases=%u gl_trace_calls=%zu gl_trace_hash=%016llx CPU_live=%zu GL_live=%u\n",success_cases,trace_calls,(unsigned long long)trace_hash,live_owners,texture_count+buffer_count);return 0;
}
static void test_decoder_failures(void){
 static const int formats[][2]={{0,0},{0,2},{0,19},{1,0},{1,1},{1,3},{1,5},{1,6},{1,14}};
 for(unsigned fi=0;fi<9;fi++)for(int site=1;site<=(formats[fi][1]==19?2:1);site++){
  Fixture f;fixture(&f,formats[fi][0],formats[fi][1],0);uint8_t *rgba=(uint8_t*)(uintptr_t)0x123;int w=99,h=98;fail_next(site);assert(tex_decode(&f.b,&f.entries[0],&rgba,&w,&h)==-2);assert(rgba==(void*)(uintptr_t)0x123&&w==99&&h==98);clean();assert(!tex_decode(&f.b,&f.entries[0],&rgba,&w,&h));assert(w==16&&h==16);free(rgba);clean();failure_cases++;
 }
 Fixture f;fixture(&f,0,0,0);uint8_t *rgba=(void*)(uintptr_t)0x123;int w=99,h=98;
 for(int bad=0;bad<5;bad++){fixture(&f,0,0,0);if(bad==0)put16(f.header+8,0);if(bad==1)f.header[4]=0xff;if(bad==2)f.entries[0].size2=1;if(bad==3){put16(f.header+8,65535);put16(f.header+10,65535);f.entries[0].size2=UINT32_MAX;}if(bad==4){f.b.wii=1;put32(f.header+4,6);put16(f.header+8,65535);put16(f.header+10,65535);f.entries[0].size2=UINT32_MAX;}size_t calls=alloc_calls;assert(tex_decode(&f.b,&f.entries[0],&rgba,&w,&h)==-1);assert(alloc_calls==calls&&rgba==(void*)(uintptr_t)0x123&&w==99&&h==98);clean();failure_cases++;}
}
static void test_texture_failures(void){
 Fixture f;fixture(&f,0,19,0);fail_next(1);assert(!rnd_create());clean();failure_cases++;
 Renderer *r=rnd_create();for(int n=0;n<32;n++){fail_next(1);assert(!bundle_tex(r,&f.b));assert(!r->ncache);fail_at=0;failure_cases++;}assert(bundle_tex(r,&f.b));rnd_free(r);clean();
 Bundle bs[17];memset(bs,0,sizeof bs);r=rnd_create();for(int n=0;n<16;n++)assert(bundle_tex(r,&bs[n]));size_t calls=alloc_calls;assert(!bundle_tex(r,&bs[16])&&r->ncache==16&&alloc_calls==calls);rnd_free(r);clean();
 for(int hud=0;hud<2;hud++)for(int site=1;site<=2;site++){
  r=rnd_create();BundleTex *bt=bundle_tex(r,&f.b);r->in_hud_quad=hud;
#ifdef RAMPAGE_XBOX
  if(hud){bt->hud_tex=calloc(bt->n,sizeof *bt->hud_tex);assert(bt->hud_tex);}
#endif
  size_t owners=live_owners;fail_next(site);assert(!texture_at(r,&f.b,1,0));assert(live_owners==owners&&!texture_count);assert(!bt->tex[1]&&(!bt->hud_tex||!bt->hud_tex[1]));fail_at=0;assert(texture_at(r,&f.b,1,0));rnd_free(r);clean();failure_cases++;
 }
#ifdef RAMPAGE_XBOX
 r=rnd_create();GLuint world=texture_at(r,&f.b,1,0);r->in_hud_quad=1;fail_next(1);assert(!texture_at(r,&f.b,1,0)&&r->cache[0].tex[1]==world&&!r->cache[0].hud_tex);fail_at=0;assert(texture_at(r,&f.b,1,0)!=world);rnd_free(r);clean();failure_cases++;
#endif
 for(int no_write=1;no_write<=2;no_write++){
  r=rnd_create();tex_failure=no_write;assert(!texture_at(r,&f.b,1,0)&&!r->cache[0].tex[1]&&!texture_count);assert(texture_at(r,&f.b,1,0));rnd_free(r);clean();failure_cases++;
 }
 for(int kind=0;kind<3;kind++){
  fixture(&f,0,kind==2?0xff:0,0);if(kind==1)f.entries[0].size2=1;r=rnd_create();uint32_t name=kind==0?3:1;assert(!texture_at(r,&f.b,name,0));assert(r->cache[0].tex[name]==~0u);size_t calls=alloc_calls;for(int i=0;i<50;i++)assert(!texture_at(r,&f.b,name,0));assert(alloc_calls==calls);rnd_free(r);clean();
 }
}
static void test_regions(void){
 Fixture f;fixture(&f,0,19,0);float reg[4]={0,.125f,.25f,.5f},reg2[4]={.5f,.25f,.25f,.25f},scale[2];
 /* metadata, decoded pixels, indexed scratch, crop; plus optional texture-pack pixels. */
 for(int pack=0;pack<2;pack++)for(int site=1;site<=4+pack;site++){
  pack_mode=pack?2:0;Renderer *r=rnd_create();GLuint kept=region_texture(r,&f.b,1,reg,scale);assert(kept);BundleTex *bt=&r->cache[0];unsigned before=texture_count;size_t owners=live_owners;
  fail_next(site);GLuint failed=region_texture(r,&f.b,1,reg2,scale);fail_at=0;
  if(pack&&site==4){/* Missing pack and pack-allocation failure deliberately share fallback semantics. */assert(failed&&bt->nreg==2);}
  else {assert(!failed&&scale[0]==1&&scale[1]==1&&bt->nreg==1&&live_owners==owners&&texture_count==before);assert(region_texture(r,&f.b,1,reg2,scale));failure_cases++;}
  assert(region_texture(r,&f.b,1,reg,scale)==kept);rnd_free(r);clean();
 }
 pack_mode=0;for(int fail=1;fail<=2;fail++){Renderer *r=rnd_create();tex_failure=fail;assert(!region_texture(r,&f.b,1,reg,scale)&&r->cache[0].nreg==0&&!texture_count);assert(region_texture(r,&f.b,1,reg,scale));rnd_free(r);clean();failure_cases++;}
 for(int bad=0;bad<3;bad++){fixture(&f,0,bad==1?0xff:0,0);Renderer *r=rnd_create();float whole[4]={0,0,1,1};assert(!region_texture(r,&f.b,bad==0?3:1,bad==2?whole:reg,scale));size_t calls=alloc_calls;assert(r->cache[0].nreg==1);for(int i=0;i<50;i++)assert(!region_texture(r,&f.b,bad==0?3:1,bad==2?whole:reg,scale));assert(alloc_calls==calls);rnd_free(r);clean();}
}
static void test_buffers(void){
 float pos[36]={0},nrm[36]={0};uint32_t idx[3]={0,1,2};MeshPart parts[2]={{.nverts=3,.ntris=1,.pos=pos,.tris=idx}};parts[1]=parts[0];SceneMesh meshes[2]={{.mesh={.parts=parts,.nparts=1}}};meshes[1]=meshes[0];
 for(int site=1;site<=3;site++){Scene *s=calloc(1,sizeof *s);s->meshes=meshes;s->nmeshes=1;Renderer *r=rnd_create();r->scene=s;fail_next(site);assert(!part_buffers(r,meshes,0)&&!buffer_count);fail_at=0;assert(part_buffers(r,meshes,0));unbind_buffers();rnd_free(r);free(s);clean();failure_cases++;}
 for(int fail=1;fail<=3;fail++){Scene *s=calloc(1,sizeof *s);s->meshes=meshes;s->nmeshes=1;Renderer *r=rnd_create();r->scene=s;buf_failure=fail;unsigned calls=data_calls;assert(!part_buffers(r,meshes,0));assert(!buffer_count&&!r->mbuf[0].pb[0].vbo&&!r->mbuf[0].pb[0].ibo&&data_calls==calls);assert(part_buffers(r,meshes,0));unbind_buffers();rnd_free(r);free(s);clean();failure_cases++;}
 Scene *s=calloc(1,sizeof *s);s->meshes=meshes;s->nmeshes=1;Renderer *r=rnd_create();r->scene=s;const PartBuf *pb=part_buffers(r,meshes,0);unbind_buffers();GLuint vbo=pb->vbo,ibo=pb->ibo;float *stage=r->stage;size_t capacity=r->nstage;unsigned data=data_calls;
 parts[0].nverts=12;parts[0].nrm=nrm;fail_next(1);assert(!part_buffers(r,meshes,0));assert(r->stage==stage&&r->nstage==capacity&&pb->vbo==vbo&&pb->ibo==ibo&&pb->nverts==3&&data_calls==data);fail_at=0;assert(part_buffers(r,meshes,0)==pb);unbind_buffers();failure_cases++;
 s->nmeshes=2;MeshBuf *old=r->mbuf;fail_next(1);assert(!part_buffers(r,meshes+1,0)&&r->mbuf==old&&r->nmbuf==1&&buffer_count==2);fail_at=0;assert(part_buffers(r,meshes+1,0));unbind_buffers();failure_cases++;
 meshes[0].mesh.parts=parts+1;fail_next(1);assert(!part_buffers(r,meshes,0)&&!r->mbuf[0].pb&&r->mbuf[0].nparts==0&&buffer_count==2);fail_at=0;assert(part_buffers(r,meshes,0));unbind_buffers();failure_cases++;
 stage=r->stage;capacity=r->nstage;rnd_scene(r,NULL);assert(!r->mbuf&&!buffer_count&&r->stage==stage&&r->nstage==capacity);rnd_scene(r,NULL);rnd_free(r);free(s);clean();
}
static void test_optional_and_independent(void){
 Fixture f;fixture(&f,0,0,0);Renderer *r=rnd_create();bundle_tex(r,&f.b);g_mips=1;unsigned images=image_calls;
 fail_next(2);GLuint id=texture_at(r,&f.b,1,0);assert(id&&image_calls==images+1&&last_min_filter==GL_LINEAR);fail_at=0;rnd_free(r);clean();failure_cases++;
 r=rnd_create();bundle_tex(r,&f.b);float reg[4]={0,0,.5f,.5f},scale[2];images=image_calls;fail_next(4);id=region_texture(r,&f.b,1,reg,scale);assert(id&&image_calls==images+1&&last_min_filter==GL_LINEAR);fail_at=0;rnd_free(r);clean();failure_cases++;g_mips=0;
 Renderer *a=rnd_create(),*b=rnd_create();GLuint aid=texture_at(a,&f.b,1,0),bid=texture_at(b,&f.b,1,0);assert(aid&&bid&&aid!=bid&&a->cache[0].tex!=b->cache[0].tex);rnd_scene(a,NULL);assert(texture_live[bid]&&!texture_live[aid]);assert(texture_at(b,&f.b,1,0)==bid);rnd_free(a);rnd_free(b);clean();
}
static int probe(void){
 Fixture f;fixture(&f,0,0,0);Renderer *r=rnd_create();fail_next(1);(void)texture_at(r,&f.b,1,0);fail_at=0;printf("probe bundle_table published=%d retry_handle=%u\n",r->ncache,texture_at(r,&f.b,1,0));rnd_free(r);clean();
 r=rnd_create();bundle_tex(r,&f.b);fail_next(1);(void)texture_at(r,&f.b,1,0);fail_at=0;printf("probe decode slot=%u retry_handle=%u\n",r->cache[0].tex[1],texture_at(r,&f.b,1,0));rnd_free(r);clean();
 r=rnd_create();bundle_tex(r,&f.b);float reg[4]={0,0,.5f,.5f},scale[2];fail_next(3);(void)region_texture(r,&f.b,1,reg,scale);fail_at=0;printf("probe region_cut published=%d retry_handle=%u\n",r->cache[0].nreg,region_texture(r,&f.b,1,reg,scale));rnd_free(r);clean();
 float pos[9]={0};uint32_t tris[3]={0,1,2};MeshPart part={.pos=pos,.tris=tris,.nverts=3,.ntris=1};SceneMesh sm={.mesh={.parts=&part,.nparts=1}};Scene *s=calloc(1,sizeof *s);s->meshes=&sm;s->nmeshes=1;r=rnd_create();r->scene=s;buf_failure=1;(void)part_buffers(r,&sm,0);unbind_buffers();(void)part_buffers(r,&sm,0);unbind_buffers();rnd_free(r);free(s);printf("probe incomplete_buffer lost_handles=%u invalid_gl=%u CPU_live=%zu\n",buffer_count,invalid_gl,live_owners);return 0;
}
int main(int argc,char **argv){
 if(argc>1&&!strcmp(argv[1],"--indexed-fault")){Fixture f;fixture(&f,0,19,0);uint8_t *rgba=NULL;int w=0,h=0;fail_next(2);int rc=tex_decode(&f.b,&f.entries[0],&rgba,&w,&h);printf("indexed scratch fault status=%d CPU_live=%zu\n",rc,live_owners);return rc==-2&&live_owners==0?0:1;}
 if(argc>1&&!strcmp(argv[1],"--healthy")){const char *path=getenv("TRACE_FILE");if(path){trace_file=fopen(path,"wb");assert(trace_file);}int rc=healthy();if(trace_file)assert(!fclose(trace_file));return rc;}
 if(argc>1&&!strcmp(argv[1],"--probe"))return probe();
 test_decoder_failures();test_texture_failures();test_regions();test_buffers();test_optional_and_independent();
 for(int i=0;i<100;i++){test_texture_failures();test_regions();test_buffers();}
 printf("failure/retry cases=%u injected_CPU_failures=%zu peak_tracked_bytes=%zu final_CPU_owners=%zu final_CPU_bytes=%zu final_GL_textures=%u final_GL_buffers=%u invalid_GL=%u\n",failure_cases,alloc_failures,peak_bytes,live_owners,live_bytes,texture_count,buffer_count,invalid_gl);clean();return 0;
}
