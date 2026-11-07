/* Fixture bundles are loaded by the complete current bundle.c outside this
 * allocation ledger. Only decoder/renderer calls below are tracked. */
#undef malloc
#undef calloc
#undef realloc
#undef free
static uint64_t actual_hash(const void *p,size_t n){uint64_t save=trace_hash;trace_hash=1469598103934665603ULL;hash_bytes(p,n);uint64_t h=trace_hash;trace_hash=save;return h;}
int main(int argc,char **argv){
 int failures=argc>1&&!strcmp(argv[1],"--failures");unsigned samples=0,faults=0;
 for(int arg=failures?2:1;arg<argc;arg++){
  FILE *file=fopen(argv[arg],"rb");assert(file);fseek(file,0,SEEK_END);long bytes=ftell(file);rewind(file);uint8_t *raw=malloc(bytes);assert(raw&&fread(raw,1,bytes,file)==(size_t)bytes);fclose(file);Bundle b;assert(!bdp_load(&b,raw,bytes));const BdpEntry *e=&b.entries[0];
  uint8_t *rgba=NULL;int w=0,h=0;size_t before=alloc_calls;assert(!tex_decode(&b,e,&rgba,&w,&h));size_t allocation_count=alloc_calls-before;uint64_t expected=actual_hash(rgba,(size_t)w*h*4);a_free(rgba);
  printf("actual %s wii=%d width=%d height=%d pixels=%016llx allocations=%zu\n",strrchr(argv[arg],'/')+1,b.wii,w,h,(unsigned long long)expected,allocation_count);
  if(failures)for(size_t site=1;site<=allocation_count;site++){
   uint8_t *out=(void*)(uintptr_t)0x123;int ow=-10,oh=-20;fail_next(site);assert(tex_decode(&b,e,&out,&ow,&oh)==-2);assert(out==(void*)(uintptr_t)0x123&&ow==-10&&oh==-20&&!live_owners);fail_at=0;assert(!tex_decode(&b,e,&out,&ow,&oh));assert(ow==w&&oh==h&&actual_hash(out,(size_t)ow*oh*4)==expected);a_free(out);faults++;
   Renderer *r=rnd_create();BundleTex *bt=bundle_tex(r,&b);fail_next(site);assert(!texture_at(r,&b,e->name,1));assert(!bt->tex[e->name]&&!texture_count);fail_at=0;assert(texture_at(r,&b,e->name,1));rnd_free(r);assert(!live_owners&&!texture_count);faults++;
   if(e->name){float reg[4]={0,0,.5f,.5f},scale[2];r=rnd_create();bt=bundle_tex(r,&b);fail_next(site+1);assert(!region_texture(r,&b,e->name,reg,scale));assert(!bt->nreg&&!texture_count);fail_at=0;assert(region_texture(r,&b,e->name,reg,scale));rnd_free(r);assert(!live_owners&&!texture_count);faults++;}
  }
  Renderer *r=rnd_create();assert(texture_at(r,&b,e->name,1));float reg[4]={0,0,.5f,.5f},scale[2];if(e->name)assert(region_texture(r,&b,e->name,reg,scale));rnd_free(r);bdp_free(&b);assert(!live_owners&&!live_bytes&&!texture_count&&!buffer_count&&!invalid_gl);samples++;
 }
 printf("actual samples=%u injected_failure_cases=%u GL_trace_calls=%zu GL_trace_hash=%016llx CPU_live=%zu GL_live=%u\n",samples,faults,trace_calls,(unsigned long long)trace_hash,live_owners,texture_count+buffer_count);return 0;
}
