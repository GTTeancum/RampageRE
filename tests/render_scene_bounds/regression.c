/* This file is included AFTER the byte-for-byte renderer fragments. */
static unsigned checks, failures, cases;
static const float identity[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #c); } } while (0)

typedef struct {
    Scene scene[2];
    SceneMesh meshes[4];
    MeshPart parts[4];
    float pos[4][9];
    DrawItem item;
} Fixture;

static Fixture *fixture(void)
{
    Fixture *f = calloc(1, sizeof *f);
    if (!f) { fputs("fixture allocation failed\n", stderr); exit(2); }
    for (unsigned i = 0; i < 2; ++i) {
        f->scene[i].meshes = f->meshes;
        f->scene[i].nmeshes = 4;
    }
    for (unsigned i = 0; i < 4; ++i) {
        f->meshes[i].mesh.parts = &f->parts[i];
        f->meshes[i].mesh.nparts = 1;
        f->parts[i].pos = f->pos[i];
        f->parts[i].nverts = 3;
        f->parts[i].ntris = 1;
    }
    memcpy(f->item.world.m, identity, sizeof identity);
    return f;
}

static void geometry(Fixture *f, unsigned i, int inside)
{
    const float x = inside ? 0.0f : 100.0f;
    const float vertices[9] = { x-1,-1,-10, x+1,-1,-10, x,1,-10 };
    memcpy(f->pos[i], vertices, sizeof vertices);
    /* Neither parts, position storage, nparts nor vertex counts change. */
}

static int seen(Renderer *r, Fixture *f, unsigned i)
{
    f->item.mesh = i;
    return item_seen(r, &f->item, identity, 1.0f, 1.0f);
}

static void allocated_unchanged(Renderer *r, MeshBound *allocation, unsigned capacity, unsigned reallocations)
{
    CHECK(r->bound == allocation);
    CHECK(r->nbound == capacity);
    CHECK(test_realloc_calls == reallocations);
}

static void compare_fresh(Renderer *r, Fixture *f, const Scene *s, unsigned i, int expected)
{
    Renderer *fresh = rnd_create();
    CHECK(fresh != NULL);
    if (!fresh) exit(2);
    rnd_scene(fresh, s);
    const int actual = seen(r, f, i);
    const int reference = seen(fresh, f, i);
    CHECK(reference == expected);
    CHECK(actual == reference);
    for (unsigned axis = 0; axis < 3; ++axis)
        CHECK(r->bound[i].c[axis] == fresh->bound[i].c[axis]);
    CHECK(r->bound[i].r == fresh->bound[i].r);
    rnd_free(fresh);
}

static void transition(int from, int to, int same_scene_address)
{
    ++cases;
    Fixture *f = fixture();
    Renderer *r = rnd_create();
    CHECK(r != NULL);
    if (!r) exit(2);
    Scene *next = &f->scene[same_scene_address ? 0 : 1];
    for (unsigned i = 0; i < 4; ++i) geometry(f, i, from);
    rnd_scene(r, &f->scene[0]);
    for (unsigned i = 0; i < 4; ++i) CHECK(seen(r, f, i) == from);
    MeshBound *allocation = r->bound;
    const unsigned capacity = r->nbound;
    const unsigned allocations_before = test_realloc_calls;
    CHECK(capacity == 4);
    for (unsigned i = 0; i < 4; ++i) {
        geometry(f, i, to);
        CHECK(next->meshes[i].mesh.parts == r->bound[i].parts);
        CHECK(next->meshes[i].mesh.nparts == r->bound[i].nparts);
        CHECK(next->meshes[i].mesh.parts[0].pos == f->pos[i]);
        CHECK(next->meshes[i].mesh.parts[0].nverts == 3);
    }
    rnd_scene(r, next);
    CHECK(r->scene == next);
    allocated_unchanged(r, allocation, capacity, allocations_before);
    for (unsigned i = 0; i < 4; ++i) {
        /* Check no realloc before fresh renderer creates its separate allocation. */
        const unsigned before = test_realloc_calls;
        (void)seen(r, f, i);
        allocated_unchanged(r, allocation, capacity, before);
    }
    const unsigned failures_before = failures;
    for (unsigned i = 0; i < 4; ++i) compare_fresh(r, f, next, i, to);
    printf("CASE %s -> %s (%s Scene address): %s\n", from ? "inside" : "outside", to ? "inside" : "outside",
           same_scene_address ? "same" : "different", failures == failures_before ? "PASS" : "FAIL");
    rnd_free(r);
    free(f);
}

static void lifecycle_edges(void)
{
    ++cases;
    const unsigned failures_before = failures;
    Renderer *r = rnd_create();
    Fixture *f = fixture();
    CHECK(r != NULL);
    if (!r) exit(2);
    /* Initial/null resets must neither dereference NULL nor allocate. */
    unsigned before = test_realloc_calls;
    rnd_scene(r, NULL);
    CHECK(r->scene == NULL);
    allocated_unchanged(r, NULL, 0, before);
    for (unsigned i = 0; i < 4; ++i) geometry(f, i, 0);
    rnd_scene(r, &f->scene[0]);
    for (unsigned i = 0; i < 4; ++i) CHECK(seen(r, f, i) == 0);
    MeshBound *allocation = r->bound;
    const unsigned capacity = r->nbound;
    before = test_realloc_calls;
    f->scene[1].nmeshes = 1;
    rnd_scene(r, &f->scene[1]);
    allocated_unchanged(r, allocation, capacity, before);
    /* Even retained entries above the smaller scene's mesh count must be invalid. */
    for (unsigned i = 0; i < capacity; ++i) {
        CHECK(r->bound[i].parts == NULL);
        CHECK(r->bound[i].nparts == 0);
    }
    geometry(f, 3, 1);
    rnd_scene(r, &f->scene[0]);
    allocated_unchanged(r, allocation, capacity, before);
    compare_fresh(r, f, &f->scene[0], 3, 1);
    before = test_realloc_calls;
    rnd_scene(r, NULL);
    CHECK(r->scene == NULL);
    allocated_unchanged(r, allocation, capacity, before);
    geometry(f, 3, 0);
    rnd_scene(r, &f->scene[0]);
    allocated_unchanged(r, allocation, capacity, before);
    compare_fresh(r, f, &f->scene[0], 3, 0);
    /* An empty scene retains storage but cannot preserve stale entries. */
    f->scene[1].nmeshes = 0;
    before = test_realloc_calls;
    rnd_scene(r, &f->scene[1]);
    allocated_unchanged(r, allocation, capacity, before);
    geometry(f, 3, 1);
    rnd_scene(r, &f->scene[0]);
    allocated_unchanged(r, allocation, capacity, before);
    compare_fresh(r, f, &f->scene[0], 3, 1);
    rnd_free(r);
    free(f);
    printf("CASE fresh/null/smaller/empty/regrown scene transitions: %s\n", failures == failures_before ? "PASS" : "FAIL");
}

static void stable_scene_controls(void)
{
    ++cases;
    const unsigned failures_before = failures;
    Renderer *r = rnd_create();
    Fixture *f = fixture();
    CHECK(r != NULL);
    if (!r) exit(2);
    geometry(f, 0, 1);
    rnd_scene(r, &f->scene[0]);
    CHECK(seen(r, f, 0) == 1);
    MeshBound *allocation = r->bound;
    const unsigned capacity = r->nbound;
    const unsigned before = test_realloc_calls;
    MeshBound bound = r->bound[0];
    for (unsigned i = 0; i < 8; ++i) {
        CHECK(seen(r, f, 0) == 1);
        CHECK(memcmp(&bound, &r->bound[0], sizeof bound) == 0);
    }
    allocated_unchanged(r, allocation, capacity, before);
    /* Skinned items continue to bypass static bounds. */
    Skin skin = {0};
    f->item.skin = &skin;
    geometry(f, 0, 0);
    CHECK(seen(r, f, 0) == 1);
    allocated_unchanged(r, allocation, capacity, before);
    rnd_free(r);
    free(f);
    printf("CASE same-scene cache hits and skin bypass: %s\n", failures == failures_before ? "PASS" : "FAIL");
}

static void capacity_growth(void)
{
    ++cases;
    const unsigned failures_before = failures;
    Fixture *f = fixture();
    Renderer *r = rnd_create();
    CHECK(r != NULL);
    if (!r) exit(2);
    f->scene[0].nmeshes = 1;
    for (unsigned i = 0; i < 4; ++i) geometry(f, i, 0);
    rnd_scene(r, &f->scene[0]);
    CHECK(seen(r, f, 0) == 0);
    CHECK(r->nbound == 1);
    for (unsigned i = 0; i < 4; ++i) geometry(f, i, 1);
    rnd_scene(r, &f->scene[1]);
    for (unsigned i = 0; i < 4; ++i) compare_fresh(r, f, &f->scene[1], i, 1);
    CHECK(r->nbound == 4);
    rnd_free(r); free(f);
    printf("CASE capacity growth with reused mesh storage: %s\n", failures == failures_before ? "PASS" : "FAIL");
}

static void radius_change(void)
{
    ++cases;
    const unsigned failures_before = failures;
    Fixture *f = fixture();
    Renderer *r = rnd_create();
    CHECK(r != NULL);
    if (!r) exit(2);
    for (unsigned step = 0; step < 3; ++step) {
        float half = step == 1 ? 20.0f : 1.0f;
        float pos[9] = {20-half,-half,-10, 20+half,-half,-10, 20,half,-10};
        memcpy(f->pos[0], pos, sizeof pos);
        rnd_scene(r, &f->scene[0]);
        compare_fresh(r, f, &f->scene[0], 0, step == 1);
    }
    rnd_free(r); free(f);
    printf("CASE radius-only geometry replacement: %s\n", failures == failures_before ? "PASS" : "FAIL");
}

static void zero_capacity(void)
{
    ++cases;
    const unsigned failures_before = failures;
    Renderer *r = rnd_create();
    CHECK(r != NULL);
    if (!r) exit(2);
    /* A defensive zero-length edge with a valid pointer: no writes allowed. */
    r->bound = malloc(sizeof *r->bound);
    CHECK(r->bound != NULL);
    if (!r->bound) exit(2);
    memset(r->bound, 0x5a, sizeof *r->bound);
    unsigned char before[sizeof *r->bound];
    memcpy(before, r->bound, sizeof before);
    rnd_scene(r, NULL);
    CHECK(r->nbound == 0);
    CHECK(memcmp(before, r->bound, sizeof before) == 0);
    rnd_free(r);
    printf("CASE nonnull cache with zero capacity: %s\n", failures == failures_before ? "PASS" : "FAIL");
}

int main(void)
{
    for (int same = 0; same < 2; ++same) {
        transition(0, 1, same);
        transition(1, 0, same);
        transition(1, 1, same);
        transition(0, 0, same);
    }
    lifecycle_edges();
    stable_scene_controls();
    capacity_growth();
    radius_change();
    zero_capacity();
    CHECK(test_delete_calls == 0); /* No fabricated texture resources. */
#ifdef RAMPAGE_XBOX
    CHECK(test_finish_calls > 0); /* Exact Xbox lifecycle branch was exercised. */
#else
    CHECK(test_finish_calls == 0);
#endif
    printf("SUMMARY cases=%u checks=%u failures=%u realloc_calls=%u GL_finish_calls=%u\n",
           cases, checks, failures, test_realloc_calls, test_finish_calls);
    return failures ? 1 : 0;
}
