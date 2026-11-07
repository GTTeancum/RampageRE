static float source_pos[360], source_nrm[360], out_pos[360], out_nrm[360], saved_pos[360], saved_nrm[360];
static uint8_t bone_ids[120][4]; static float weights[120][4]; static int32_t particles[120];
int main(void)
{
    Scene *s = calloc(1, sizeof *s); assert(s);
    SceneMesh mesh = {0}; MeshPart part = {.pos=source_pos, .nrm=source_nrm, .nverts=120};
    mesh.mesh.parts = &part; mesh.mesh.nparts = 1; s->meshes = &mesh;
    TreeNode nodes[4] = {0}; Mat4 transforms[4]; Instance in = {0};
    in.tree.nodes = nodes; in.tree.n = 4; in.node = transforms; in.base = mat_identity();
    Skin sk = {.nverts=120, .pos=out_pos, .nrm=out_nrm, .bones=bone_ids, .weights=weights,
               .particle=particles, .cloth_started=1};
    DrawItem it = {.mesh=0, .skin=&sk, .instance=-1};
    struct Cloth cloth = {.n=78, .nodes=4}; sk.cloth = &cloth;
    for (int j=0; j<4; j++) {
        transforms[j] = mat_identity(); transforms[j].m[12] = j*.1f;
        memcpy(nodes[j].inverse_bind, mat_identity().m, 64); nodes[j].mirror = 3-j;
    }
    for (int j=0; j<120; j++) {
        for (int k=0; k<3; k++) {
            source_pos[3*j+k] = j*.12f+k; source_nrm[3*j+k] = k==2 ? 1 : 0;
            cloth.pos[3*j+k] = j*.3f+k; cloth.nrm[3*j+k] = k==1 ? 1 : 0;
        }
        for (int k=0; k<4; k++) { bone_ids[j][k] = k; weights[j][k] = .25f; }
    }
    unsigned cases = 0; const int sizes[] = {1, 13, 32, 78};
    for (int debug=0; debug<2; debug++) for (int mirrored=0; mirrored<2; mirrored++)
    for (int normals=0; normals<2; normals++) for (int count=0; count<4; count++) {
        debug_flag = debug; in.mirrored = mirrored; part.nrm = normals ? source_nrm : NULL;
        cloth.n = sizes[count]; memcpy(cloth.world, mat_identity().m, 64);
        for (int j=0; j<120; j++) particles[j] = j%7==0 ? -1 : j%7==1 ? cloth.n : j%cloth.n;
        /* Rigid pose is the expected result for unmapped vertices and OOM fallback. */
        sk.cloth = NULL; skin_item(s, &in, &it, 1); sk.cloth = &cloth;
        memcpy(saved_pos, out_pos, sizeof out_pos); memcpy(saved_nrm, out_nrm, sizeof out_nrm);
        diagnostic[0] = 0; skin_item(s, &in, &it, 1);
        for (int j=0; j<120; j++) {
            int p = particles[j], mapped = p>=0 && p<cloth.n;
            assert(!memcmp(out_pos+3*j, mapped ? cloth.pos+3*p : saved_pos+3*j, 12));
            assert(!memcmp(out_nrm+3*j, mapped ? cloth.nrm+3*p : saved_nrm+3*j, 12));
        }
        assert((diagnostic[0] != 0) == debug); cases++;
    }
    assert(allocations == 4 && live_bytes == 1872);
    printf("cloth: %u mapping/mirror/normal/debug cases; 4 capacity growths, retained=1872 bytes\n", cases);
    unsigned before = allocations, freed_before = frees;
    for (int frame=0; frame<100; frame++) for (int k=0; k<6; k++) {
        cloth.n = sizes[k%4]; skin_item(s, &in, &it, 1);
    }
    assert(allocations == before && frees == freed_before);
    printf("cloth warm frames: 600 calls, 0 allocations/frees\n");
    void *old = s->cloth_scratch; uint32_t oldcap = s->cloth_scratch_particles;
    cloth.n = 80; sk.cloth = NULL; skin_item(s, &in, &it, 1); sk.cloth = &cloth;
    memcpy(saved_pos, out_pos, sizeof out_pos); memcpy(saved_nrm, out_nrm, sizeof out_nrm);
    fail_next = 1; skin_item(s, &in, &it, 1);
    assert(s->cloth_scratch == old && s->cloth_scratch_particles == oldcap && live_bytes == 1872);
    assert(!memcmp(saved_pos, out_pos, sizeof out_pos) && !memcmp(saved_nrm, out_nrm, sizeof out_nrm));
    skin_item(s, &in, &it, 1); assert(s->cloth_scratch_particles == 80 && live_bytes == 1920);
    s->meshes = NULL; scene_free(s);
    assert(!live_bytes && !s->cloth_scratch && !s->cloth_scratch_particles);
    s->meshes = &mesh; fail_next = 1; skin_item(s, &in, &it, 1);
    assert(!s->cloth_scratch && !live_bytes);
    assert(!memcmp(saved_pos, out_pos, sizeof out_pos) && !memcmp(saved_nrm, out_nrm, sizeof out_nrm));
    skin_item(s, &in, &it, 1); assert(s->cloth_scratch && live_bytes == 1920);
    Scene *other = calloc(1, sizeof *other); assert(other); other->meshes = &mesh;
    skin_item(other, &in, &it, 1); assert(other->cloth_scratch != s->cloth_scratch && live_bytes == 3840);
    s->meshes = NULL; scene_free(s); assert(live_bytes == 1920);
    skin_item(other, &in, &it, 1); other->meshes = NULL; scene_free(other); assert(!live_bytes);
    scene_free(s); scene_free(other); free(other); free(s);
    printf("cloth: initial/growth OOM rigid fallback, retry, two scenes, teardown PASS; live scratch=0\n");
    return 0;
}
