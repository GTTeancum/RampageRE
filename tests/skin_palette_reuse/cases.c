/* Current production helpers, skinning and item traversal are extracted by run.py. */
int main(void)
{
    static Scene scenes[2];
    Instance instances[2][3] = {0};
    TreeNode nodes[3][257] = {0};
    Mat4 poses[2][3][257];
    SceneMesh mesh = {0};
    MeshPart part = {0};
    float positions[6] = {-1, 2, 3, 4, -5, 6};
    float normals[6] = {1, 0, -0.0f, 0, 1, 1};
    uint8_t bones[2][4] = {{0, 1, 255, 254}, {255, 1, 0, 2}};
    float weights[2][4] = {{0.1f, 0.2f, 0.3f, 0.4f}, {-1, 0, 0.3f, 0.7f}};
    Skin skins[2][10] = {0};
    DrawItem items[2][10] = {0};
    float output[2][10][6], output_normals[2][10][6];
    float reference_output[10][6], reference_normals[10][6];
    Mat4 reference_world[10];
    /* A, unskinned B, hidden C, A, B, B, A, A, B, A.
     * Skipped/unskinned items must not invalidate A. With all=1, C must. */
    const int sequence[10] = {0, 1, 2, 0, 1, 1, 0, 0, 1, 0};
    const int scene_order[4] = {0, 1, 1, 0};
    const unsigned sizes[] = {0, 1, 2, 255, 256, 257};
    unsigned passes = 0, skin_visits = 0;
    mesh.mesh.nparts = 1;
    mesh.mesh.parts = &part;
    part.nverts = 2;
    part.pos = positions;
    for (int k = 0; k < 3; k++) {
        for (unsigned j = 0; j < 257; j++) {
            Mat4 inverse = mat_identity();
            inverse.m[13] = -(float)j * 0.25f;
            memcpy(nodes[k][j].inverse_bind, inverse.m, sizeof inverse.m);
            nodes[k][j].mirror = j % 3 == 0 ? -1 : j % 3 == 1 ? 999 : 256 - (int)j;
        }
    }
    for (int scene = 0; scene < 2; scene++) {
        Scene *s = &scenes[scene];
        s->meshes = &mesh;
        s->nmeshes = 1;
        s->instances = instances[scene];
        s->ninstances = 3;
        s->items = items[scene];
        s->nitems = 10;
        for (int k = 0; k < 3; k++) {
            Instance *in = &instances[scene][k];
            in->tree.nodes = nodes[k];
            in->node = poses[scene][k];
            in->base = mat_identity();
            in->base.m[12] = 100 * scene + k;
            in->dynamic = 1;
            in->vehicle = -1;
            in->hidden = k == 2;
            for (unsigned j = 0; j < 257; j++) {
                poses[scene][k][j] = mat_identity();
                poses[scene][k][j].m[12] = j * 0.125f + k + 10 * scene;
                poses[scene][k][j].m[5] = 0.5f;
            }
        }
        for (int i = 0; i < 10; i++) {
            Skin *sk = &skins[scene][i];
            sk->bones = bones;
            sk->weights = weights;
            sk->pos = output[scene][i];
            sk->nrm = output_normals[scene][i];
            sk->nverts = 2;
            items[scene][i].instance = sequence[i];
            items[scene][i].node = -1;
            items[scene][i].skin = i == 1 ? NULL : sk;
        }
    }
    for (int size = 0; size < 6; size++)
    for (int mirrored = 0; mirrored < 4; mirrored++)
    for (int with_normals = 0; with_normals < 2; with_normals++)
    for (int all = 0; all < 2; all++)
    for (int frame = 0; frame < 4; frame++) {
        int scene = scene_order[frame];
        Scene *s = &scenes[scene];
        part.nrm = with_normals ? normals : NULL;
        for (int k = 0; k < 3; k++) {
            instances[scene][k].tree.n = sizes[size];
            instances[scene][k].mirrored = (mirrored >> (k & 1)) & 1;
            /* Same Instance addresses survive calls, but their pose changes. */
            poses[scene][k][0].m[12] += 0.125f;
        }
        unsigned long before_ref = reference_builds, before_candidate = candidate_builds;
        unsigned long before_ref_mults = reference_mults, before_candidate_mults = candidate_mults;
        for (int trial = 0; trial < 2; trial++) {
            for (int i = 0; i < 10; i++) {
                items[scene][i].world = mat_identity();
                items[scene][i].world.m[12] = -1000.0f - i;
                for (int j = 0; j < 6; j++) {
                    output[scene][i][j] = -123.25f;
                    output_normals[scene][i][j] = 456.5f;
                }
            }
            if (!trial) {
                reference_pass(s, all);
                memcpy(reference_output, output[scene], sizeof reference_output);
                memcpy(reference_normals, output_normals[scene], sizeof reference_normals);
                for (int i = 0; i < 10; i++) reference_world[i] = items[scene][i].world;
            } else {
                candidate_pass(s, all);
                assert(!memcmp(reference_output, output[scene], sizeof reference_output));
                assert(!memcmp(reference_normals, output_normals[scene], sizeof reference_normals));
                for (int i = 0; i < 10; i++)
                    assert(!memcmp(&reference_world[i], &items[scene][i].world, sizeof(Mat4)));
            }
        }
        unsigned ref_count = all ? 9 : 8, candidate_count = all ? 7 : 5;
        unsigned nb = sizes[size] < 256 ? sizes[size] : 256;
        assert(reference_builds - before_ref == ref_count);
        assert(candidate_builds - before_candidate == candidate_count);
        assert(reference_mults - before_ref_mults == ref_count * nb);
        assert(candidate_mults - before_candidate_mults == candidate_count * nb);
        skin_visits += ref_count;
        passes++;
    }
    assert(passes == 384 && skin_visits == 3264);
    assert(reference_builds == 3264 && candidate_builds == 2304);
    assert(reference_mults == 418880 && candidate_mults == 295680);
    printf("PASS: %u item-loop passes, %u skin visits; palette builds %lu -> %lu; "
           "palette multiplies %lu -> %lu; bitwise positions/normals/worlds equal\n",
           passes, skin_visits, reference_builds, candidate_builds, reference_mults, candidate_mults);
    return 0;
}
