static void pump_output(Renderer*r){(void)r;}
static GLuint texture(Renderer*r,const Bundle*b,uint32_t n){(void)r;(void)b;return n;}
static uint32_t shown_texture(Renderer*r,const Bundle*b,uint32_t n,double t){(void)r;(void)b;(void)t;return n;}
static GLuint region_texture(Renderer*r,const Bundle*b,uint32_t n,const float reg[4],float scale[2]){(void)r;(void)b;(void)reg;scale[0]=2;scale[1]=4;return n+32;}
static const PartBuf *part_buffers(Renderer*r,const SceneMesh*s,uint32_t n){(void)r;(void)s;(void)n;return NULL;}
static int cube_material(const Scene*s,const SceneMesh*m,uint32_t n){(void)s;(void)m;(void)n;return 0;}
static void sphere_coords(int n){tr("sphere_coords");vi(n);}
static unsigned nrealloc, nfree; static size_t live_bytes; static int reject_realloc;
static struct { void *p; size_t n; } blocks[16];
static void *count_realloc(void*p,size_t n){
    nrealloc++; if(reject_realloc)return NULL;
    unsigned k=0;for(;k<16;k++)if(blocks[k].p==p)break;assert(k<16);
    void*q=realloc(p,n);if(q){live_bytes+=n-blocks[k].n;blocks[k].p=q;blocks[k].n=n;}return q;
}
static void count_free(void*p){
    if(p)for(unsigned k=0;k<16;k++)if(blocks[k].p==p){live_bytes-=blocks[k].n;blocks[k].p=NULL;blocks[k].n=0;nfree++;break;}
    free(p);
}
#define realloc count_realloc
#define free count_free
struct Water {int nv;}; static int null_uv;
int water_vertex_count(const Water*w){return w->nv;}
void water_env_uv(const Water*w,const float p[3],float*uv){(void)w;(void)p;if(!uv)null_uv++;}
const char *water_texture(const Water*w){(void)w;return "water";}
const uint32_t *water_triangles(const Water*w,int*n){static uint32_t t[3];(void)w;*n=1;return t;}
const float *water_positions(const Water*w){static float p[9];(void)w;return p;}

