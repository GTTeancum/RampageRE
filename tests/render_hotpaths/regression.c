/* Included after current production definitions. GL and water are boundary stubs. */
int main(void)
{
    BdpEntry entries[3] = {0};
    int32_t by[3] = {-1, 0, 1}, mats[3] = {-1, 0, -1}, texs[3] = {-1, -1, 1};
    const char *names[3] = {"", "material", "water"};
    unsigned char data[96] = {0};
    entries[0].data = data; entries[0].size = 96; entries[0].type = ET_MATERIAL;
    entries[1].type = ET_TEXTURE;
    Bundle b = {0};
    b.nnames = 3; b.names = names; b.entries = entries; b.by_name = by;
    b.mat_by_name = mats; b.tex_by_name = texs;
    Scene *s = calloc(1, sizeof *s); assert(s);
    s->city = &b; s->cloud_rate[0] = 0.12345f; s->cloud_rate[1] = -0.23456f;
    float p[12] = {0}, n[12] = {0}, u[8] = {0}, c[16] = {0};
    uint32_t idx[3] = {0, 1, 2};
    MeshPart mp = {.material=1, .pos=p, .nrm=n, .uv=u, .col=c, .tris=idx, .nverts=3, .ntris=1};
    SceneMesh sm = {.mesh={.parts=&mp, .nparts=1}, .b=&b};
    Skin sk = {.pos=p, .nrm=n};
    Renderer r = {.scene=s};
    unsigned cases = 0, calls = 0;
    const double times[] = {0, 1.5, -2.3, 123456.75};
    for (int kind=0; kind<=8; kind++)
    for (int flags=0; flags<8; flags++)
    for (int attrs=0; attrs<8; attrs++)
    for (int scroll=0; scroll<2; scroll++)
    for (int ti=0; ti<4; ti++) {
        memset(data, 0, sizeof data); data[2] = kind;
        uint32_t fl = (flags&1 ? MAT_REGION : 0) | (flags&2 ? 0x100000 : 0) | (flags&4 ? MAT_TWO_SIDED : 0);
        memcpy(data+4, &fl, 4);
        float color[4] = {.8f, .4f, .2f, flags&4 ? .5f : 1};
        memcpy(data+0x18, color, 16); data[0x38] = 2; data[0x3A] = 2;
        mp.nrm = attrs&1 ? n : NULL; mp.uv = attrs&2 ? u : NULL; mp.col = attrs&4 ? c : NULL;
        s->cloud_material = scroll ? 1 : 0;
        nt = nfmod = ntranslate = 0; r.drawn = 0;
        draw_part(&r, &sm, attrs==7 ? &sk : NULL, 0, 0, times[ti]);
        assert(nfmod == (scroll ? 2u : 0u));
        if (scroll) {
            float expected[3] = {(float)fmod(s->cloud_rate[0]*times[ti], 1), (float)fmod(s->cloud_rate[1]*times[ti], 1), 0};
            assert(ntranslate >= 1 && !memcmp(translated, expected, sizeof expected));
        } else assert(!ntranslate);
        calls += nfmod; cases++;
    }
    printf("cloud drift: %u combinations, %u fmod calls, exact scrolling values\n", cases, calls);

    ShadowList shadows = {0}; ShadowBatch batches[3] = {{0}}; ShadowVert verts[3] = {{0}};
    shadows.b = batches; shadows.v = verts; s->shadows = &shadows;
    unsigned shadow_cases = 0, avoided = 0;
    for (int mask=0; mask<8; mask++) for (int pass=0; pass<3; pass++) {
        shadows.nb = 0;
        for (int j=0; j<3; j++) if (mask & (1<<j)) {
            batches[shadows.nb].pass = j; batches[shadows.nb].count = 3; shadows.nb++;
        }
        nt = npush = ndraw = 0; draw_shadows(&r, pass);
        if (mask & (1<<pass)) assert(npush == 1 && ndraw == 1);
        else { assert(!nt && !npush && !ndraw); avoided++; }
        shadow_cases++;
    }
    printf("shadows: %u pass/mask cases, %u empty passes perform no GL calls\n", shadow_cases, avoided);

    struct Water w = {121}; s->water[0] = &w; s->nwater = 1; Camera cam = {0};
    Renderer *rr = rnd_create(); rr->scene = s;
    assert(!rr->water_uv && !rr->nwater_uv);
    w.nv = 0; nt = ndraw = 0; draw_water(rr, s, &cam);
    assert(!nrealloc && !ndraw && !null_uv);
    w.nv = -1; nt = ndraw = 0; draw_water(rr, s, &cam);
    assert(!nrealloc && !ndraw && !null_uv);
    /* This size overflow is possible on a 32-bit size_t ABI, not a 64-bit host's int range. */
    if (SIZE_MAX / (2 * sizeof(float)) < INT32_MAX) {
        w.nv = (int)(SIZE_MAX / (2 * sizeof(float)) + 1);
        nt = ndraw = 0; draw_water(rr, s, &cam);
        assert(!nrealloc && !ndraw && !null_uv);
    }
    w.nv = 121;
    nt = 0; draw_water(rr, s, &cam); void *old = rr->water_uv;
    assert(old && rr->nwater_uv == 121 && nrealloc == 1 && live_bytes == 968);
    nt = 0; draw_water(rr, s, &cam); assert(nrealloc == 1);
    w.nv = 122; reject_realloc = 1; nt = ndraw = 0; draw_water(rr, s, &cam);
    assert(nrealloc == 2 && rr->water_uv == old && rr->nwater_uv == 121 && !ndraw && !null_uv);
    reject_realloc = 0; nt = 0; draw_water(rr, s, &cam);
    assert(nrealloc == 3 && rr->nwater_uv == 122 && live_bytes == 976);
    /* Scene switches retain capacity without transferring ownership. */
    nt = 0; rnd_scene(rr, NULL); assert(rr->water_uv && rr->nwater_uv == 122);
    nt = 0; rnd_free(rr); assert(!live_bytes && nfree == 1);
    rr = rnd_create(); rr->scene = s; reject_realloc = 1; nt = ndraw = 0; draw_water(rr, s, &cam);
    assert(!rr->water_uv && !rr->nwater_uv && !ndraw && !null_uv);
    reject_realloc = 0; nt = 0; draw_water(rr, s, &cam);
    assert(rr->water_uv && rr->nwater_uv == 122);
    Renderer *other = rnd_create(); other->scene = s; nt = 0; draw_water(other, s, &cam);
    assert(other->water_uv && other->water_uv != rr->water_uv && live_bytes == 1952);
    nt = 0; rnd_free(rr); assert(live_bytes == 976 && nfree == 2);
    nt = 0; draw_water(other, s, &cam); nt = 0; rnd_free(other);
    assert(!live_bytes && nfree == 3 && !null_uv);
    printf("water: zero/negative count, reuse, scene switch, two renderers, initial/growth failures, retry, teardown PASS; live scratch=0\n");
    free(s);
    return 0;
}
