/* Pinned historical control only: pre-lighting-cleanup functions. */
static Mesh mesh_copy_lit(const Mesh *m)
{
    Mesh c = { 0 };
    c.parts = calloc(m->nparts ? m->nparts : 1, sizeof *c.parts);
    c.nparts = m->nparts;
    for (uint32_t i = 0; i < m->nparts; i++) {
        const MeshPart *p = &m->parts[i];
        MeshPart *q = &c.parts[i];
        size_t nv = p->nverts ? p->nverts : 1, nt = p->ntris ? p->ntris : 1;
        q->material = p->material;
        q->nverts = p->nverts;
        q->ntris = p->ntris;
        q->blend = p->blend;
        q->pos = malloc(12 * nv);
        memcpy(q->pos, p->pos, 12 * (size_t)p->nverts);
        q->tris = malloc(12 * nt);
        memcpy(q->tris, p->tris, 12 * (size_t)p->ntris);
        if (p->uv) {
            q->uv = malloc(8 * nv);
            memcpy(q->uv, p->uv, 8 * (size_t)p->nverts);
        }
        if (p->nrm && !p->col) {
            q->col = malloc(16 * nv);
            for (uint32_t k = 0; k < 4 * p->nverts; k++)
                q->col[k] = 1;
            continue;
        }
        if (p->nrm) {
            q->nrm = malloc(12 * nv);
            memcpy(q->nrm, p->nrm, 12 * (size_t)p->nverts);
        }
        if (p->col) {
            q->col = malloc(16 * nv);
            memcpy(q->col, p->col, 16 * (size_t)p->nverts);
        }
    }
    return c;
}
static void light_items_make(Scene *s, SceneBuilding *sb)
{
    sb->nlit_items = 0;
    for (uint32_t i = 0; i < s->nitems && sb->nlit_items < 16; i++) {
        DrawItem *it = &s->items[i];
        int roof = 0;
        for (int k = 0; k < 4; k++)
            roof |= sb->roof_inst[k] >= 0 && it->instance == sb->roof_inst[k];
        if (it->skin || (it->instance != sb->ground && it->instance != sb->top && !roof))
            continue;
        uint32_t src = it->mesh;
        Mesh copy = mesh_copy_lit(&s->meshes[src].mesh);
        uint32_t mi = scene_add_mesh(s, copy, s->meshes[src].b);
        s->items[i].mesh = mi;
        sb->lit_item[sb->nlit_items] = (int)i;
        sb->lit_src[sb->nlit_items++] = (int)src;
    }
}
static void ground_cracks_make(Scene *s, SceneBuilding *sb)
{
    sb->gf_item = sb->gf_part = -1;
    int k = 0;
    while (k < sb->nlit_items && (sb->ground < 0 || s->items[sb->lit_item[k]].instance != sb->ground))
        k++;
    if (k == sb->nlit_items || sb->mesh < 0 || s->meshes[sb->mesh].mesh.nparts < 3)
        return;
    const Mesh *src = &s->meshes[sb->lit_src[k]].mesh;
    uint32_t pi = 0;
    while (pi < src->nparts && !(src->parts[pi].nrm && src->parts[pi].uv && !src->parts[pi].col))
        pi++;
    if (pi == src->nparts)
        return;
    const MeshPart *sp = &src->parts[pi];
    Mesh *m = &s->meshes[s->items[sb->lit_item[k]].mesh].mesh;
    MeshPart *parts = realloc(m->parts, sizeof *parts * (m->nparts + 1));
    if (!parts)
        return;
    m->parts = parts;
    MeshPart *p = &parts[m->nparts];
    memset(p, 0, sizeof *p);
    size_t nv = sp->nverts ? sp->nverts : 1, nt = sp->ntris ? sp->ntris : 1;
    p->material = s->meshes[sb->mesh].mesh.parts[2].material;   /* the facade's cracks' CrackTexture */
    p->blend = 1;
    p->nverts = sp->nverts;
    p->pos = malloc(12 * nv);
    memcpy(p->pos, sp->pos, 12 * (size_t)sp->nverts);
    p->tris = malloc(12 * nt);
    memcpy(p->tris, sp->tris, 12 * (size_t)sp->ntris);
    p->uv = malloc(8 * nv);
    p->col = calloc(4 * nv, sizeof *p->col);
    sb->gf_vert = calloc(nv, sizeof *sb->gf_vert);
    float w5 = CELL * (float)sb->b.width, d5 = CELL * (float)sb->b.depth, ck = sb->crack_k;
    for (uint32_t v = 0; v < sp->nverts; v++) {
        const float *x = sp->pos + 3 * v, *n = sp->nrm + 3 * v;
        int f = (int)(42.5f * x[2] + 0.5f);
        sb->gf_vert[v][0] = (uint8_t)ground_zone(x, n, w5, d5);
        sb->gf_vert[v][1] = (uint8_t)(f < 0 ? 0 : f > 255 ? 255 : f);
        float u, t;
        if (fabsf(n[2]) > 0.9f) {
            u = ck * (x[0] + d5);
            t = 1 - ck * (x[1] + 0.5f * d5 + 3);
        } else {
            t = 1 - ck * x[2];
            u = fabsf(n[1]) > fabsf(n[0]) ? ck * (x[0] + d5) : x[0] > 0 ? ck * (w5 + x[1] + 0.5f * d5) :
                ck * (0.5f * d5 - x[1]);
        }
        p->uv[2 * v] = fixed_412(u);
        p->uv[2 * v + 1] = fixed_412(t);
    }
    sb->gf_part = (int)m->nparts++;
    sb->gf_item = sb->lit_item[k];
    sb->gf_ntris = (int)sp->ntris;
}
static void light_buildings(Scene *s)
{
    for (uint32_t i = 0; i < s->nbuildings; i++) {
        shade_neighbours(s, i);
        light_items_make(s, &s->buildings[i]);
        ground_cracks_make(s, &s->buildings[i]);
    }
    for (uint32_t i = 0; i < s->nscenery; i++)
        light_items_make(s, &s->scenery[i]);
    for (uint32_t i = 0; i < s->nbuildings; i++) {
        light_building(s, &s->buildings[i], 1);
        const SceneBuilding *sb = &s->buildings[i];
        if (getenv("RAMPAGE_TRACE") && (sb->shade_faces[FACE_FRONT] || sb->shade_faces[FACE_RIGHT]))
            printf("building %u (x %.0f .. %.0f): its %s%s%s in the shadow of building %d / %d\n", i, sb->b.min[0],
                   sb->b.max[0], sb->shade_faces[FACE_FRONT] ? "front" : "", sb->shade_faces[FACE_FRONT] &&
                   sb->shade_faces[FACE_RIGHT] ? " and " : "", sb->shade_faces[FACE_RIGHT] ? "right face" : "",
                   sb->shade_nb[0], sb->shade_nb[1]);
    }
    for (uint32_t i = 0; i < s->nscenery; i++)
        light_building(s, &s->scenery[i], 0);
}
static int build_tail(Scene *s) {
    light_buildings(s);
    return 0;
failed:
    scene_free(s);
    return -1;
}
