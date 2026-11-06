/* Read-only host probe linked to the production C asset readers. No graphics/SDL. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "data.h"

static uint64_t hash(uint64_t h, const void *data, size_t n) {
    const unsigned char *p = data;
    while (n--) h = (h ^ *p++) * UINT64_C(1099511628211);
    return h;
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 3;
    fseek(f, 0, SEEK_END); long bytes = ftell(f); rewind(f);
    if (bytes <= 0 || bytes > UINT32_MAX) { fclose(f); return 4; }
    unsigned char *raw = malloc((size_t)bytes);
    if (!raw || fread(raw, 1, (size_t)bytes, f) != (size_t)bytes) return 5;
    fclose(f);
    Bundle b;
    if (bdp_load(&b, raw, (uint32_t)bytes)) return 6;
    uint64_t verts=0, tris=0, rgba_bytes=0, corners=UINT64_C(14695981039346656037);
    uint64_t pixels=UINT64_C(14695981039346656037), pose=UINT64_C(14695981039346656037);
    unsigned meshes=0, clips=0, trees=0;
    for (uint32_t i=0; i<b.nentries; ++i) {
        const BdpEntry *e = &b.entries[i];
        if (e->type == ET_TEXTURE) {
            uint8_t *rgba=NULL; int w=0,h=0;
            if (tex_decode(&b,e,&rgba,&w,&h)) return 7;
            rgba_bytes += (uint64_t)w*h*4;
            pixels=hash(pixels,rgba,(size_t)w*h*4);
            free(rgba);
        } else if (e->type == ET_MESH || e->type == ET_SKINNED) {
            Mesh m;
            if (mesh_read(e,&m) || !m.nparts) return 8;
            ++meshes;
            for (uint32_t p=0; p<m.nparts; ++p) {
                MeshPart *s=&m.parts[p]; verts+=s->nverts; tris+=s->ntris;
                corners=hash(corners,&s->material,sizeof s->material);
                for (uint32_t t=0; t<s->ntris*3; ++t) {
                    uint32_t v=s->tris[t]; if (v>=s->nverts) return 9;
                    corners=hash(corners,s->pos+3*v,12);
                    if (s->nrm) corners=hash(corners,s->nrm+3*v,12);
                    if (s->uv) corners=hash(corners,s->uv+2*v,8);
                    if (s->col) corners=hash(corners,s->col+4*v,16);
                    if (s->bones) corners=hash(corners,s->bones[v],4);
                    if (s->weights) corners=hash(corners,s->weights[v],16);
                }
            }
            mesh_free(&m);
        } else if (e->type == ET_TREE) {
            Tree t; if (tree_read(&b,e,NULL,&t)) return 10; ++trees;
            for (uint32_t j=0; j<t.n; ++j) {
                pose=hash(pose,&t.nodes[j].parent,sizeof t.nodes[j].parent);
                pose=hash(pose,t.nodes[j].t,sizeof t.nodes[j].t);
                pose=hash(pose,t.nodes[j].q,sizeof t.nodes[j].q);
                pose=hash(pose,t.nodes[j].inverse_bind,sizeof t.nodes[j].inverse_bind);
            }
            tree_free(&t);
        } else if (e->type == ET_ANIM) {
            Clip c; if (clip_read(e,&c)) return 11; ++clips;
            for (unsigned sample=0; sample<5; ++sample) {
                for (uint32_t bone=0; bone<c.bones; ++bone) {
                    float t[3]={0},q[4]={0,0,0,1};
                    clip_pose(&c,c.duration*sample/4.0,bone,t,q);
                    for (unsigned k=0;k<3;++k) if (!isfinite(t[k])) return 12;
                    for (unsigned k=0;k<4;++k) if (!isfinite(q[k])) return 12;
                    pose=hash(pose,t,sizeof t);pose=hash(pose,q,sizeof q);
                }
            }
            clip_free(&c);
        }
    }
    printf("{\"vertices\":%llu,\"triangles\":%llu,\"rgba_base_bytes\":%llu,\"meshes\":%u,\"clips\":%u,\"trees\":%u,\"corners\":\"%016llx\",\"pixels\":\"%016llx\",\"pose\":\"%016llx\"}\n",
       (unsigned long long)verts,(unsigned long long)tris,(unsigned long long)rgba_bytes,meshes,clips,trees,
       (unsigned long long)corners,(unsigned long long)pixels,(unsigned long long)pose);
    bdp_free(&b);
    return 0;
}
