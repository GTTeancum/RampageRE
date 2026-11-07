#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "city.h"

#include "city-status.inc"
#define CELL 5.0f
#define STOREY 3.0f

typedef struct { void *p; size_t n; } Allocation;
static Allocation allocations[128];
static size_t live, bytes, peak, calls, fail_at, made, released;
static void check_guards(Allocation a)
{
    for (size_t k = 0; k < 64; k++) assert(((unsigned char *)a.p)[a.n + k] == 0xA5);
}
static size_t locate(void *p)
{
    for (size_t k = 0; k < 128; k++) if (allocations[k].p == p) return k;
    assert(!"free/realloc of an unowned allocation"); return 0;
}
static void track(void *p, size_t n)
{
    assert(p && live < 128 && n <= 1048576);
    size_t k = 0;
    while (allocations[k].p) k++;
    allocations[k] = (Allocation){ p, n };
    memset((unsigned char *)p + n, 0xA5, 64);
    live++; bytes += n; made++;
    if (bytes > peak) peak = bytes;
}
static int denied(void) { return ++calls == fail_at; }
static void *test_malloc(size_t n)
{
    if (denied()) return NULL;
    assert(n <= 1048576); void *p = malloc(n + 64); track(p, n); return p;
}
static void *test_calloc(size_t n, size_t z)
{
    assert(!z || n <= SIZE_MAX / z);
    void *p = test_malloc(n * z); if (p) memset(p, 0, n * z); return p;
}
static void *test_realloc(void *p, size_t n)
{
    if (denied()) return NULL;
    if (!p) { void *q = malloc(n + 64); track(q, n); return q; }
    size_t k = locate(p); check_guards(allocations[k]);
    size_t old = allocations[k].n;
    void *q = realloc(p, n + 64); assert(q);
    allocations[k] = (Allocation){ q, n };
    memset((unsigned char *)q + n, 0xA5, 64);
    bytes = bytes - old + n; if (bytes > peak) peak = bytes;
    return q;
}
static void test_free(void *p)
{
    if (!p) return;
    size_t k = locate(p); check_guards(allocations[k]);
    bytes -= allocations[k].n; live--; released++;
    allocations[k] = (Allocation){ 0 }; free(p);
}
static void start(size_t failure)
{
    assert(!live && !bytes); calls = 0; fail_at = failure;
}
/* The synthetic facade's records are integer tile numbers. */
float record_num(Record r) { return (float)r.value; }
/* Map initialization must never own any of these resources. These stubs make
 * an unexpected destructor call a test failure rather than hiding it. */
void water_free(Water *p) { (void)p; assert(!"unexpected water"); }
void cloth_free(Cloth *p) { (void)p; assert(!"unexpected cloth"); }
void tree_free(Tree *p) { (void)p; assert(!"unexpected tree"); }
void clip_free(Clip *p) { (void)p; assert(!"unexpected clip"); }
void people_free(People *p) { (void)p; assert(!"unexpected people"); }
void building_free(Building *p) { (void)p; assert(!"unexpected building"); }
void bdp_free(Bundle *p) { (void)p; assert(!"unexpected bundle"); }

#define malloc test_malloc
#define calloc test_calloc
#define realloc test_realloc
#define free test_free
#include "city_extract.inc"
#undef malloc
#undef calloc
#undef realloc
#undef free

static uint64_t hash_bytes(uint64_t h, const void *p, size_t n)
{
    const unsigned char *s = p;
    for (size_t k = 0; k < n; k++) h = (h ^ s[k]) * UINT64_C(1099511628211);
    return h;
}
static uint64_t facade_hash(const Mesh *m)
{
    assert(m->nparts == 5);
    const MeshPart *p = m->parts;
    uint64_t h = UINT64_C(14695981039346656037);
    uint32_t meta[] = { p->material, p->nverts, p->ntris };
    h = hash_bytes(h, meta, sizeof meta);
    h = hash_bytes(h, p->pos, 12u * p->nverts);
    h = hash_bytes(h, p->col, 16u * p->nverts);
    h = hash_bytes(h, p->uv, 8u * p->nverts);
    h = hash_bytes(h, p->tris, 12u * p->ntris);
    for (int k = 1; k < 5; k++) {
        MeshPart zero = { 0 }; assert(!memcmp(&m->parts[k], &zero, sizeof zero));
    }
    return h;
}
int main(void)
{
    assert_city_status_contract();
    /* Actual map-prefix rejection must preserve City's pre-attempt primitives. */
    { City c={0}, saved; Scene s={0}, empty={0}; c.load_failed=1;
      c.create_crowd=1; c.nrunners=3; c.runners[2].at=77; saved=c;
      start(0); assert(map_build(&c,"absent",0,&s)==-1);
      assert(!calls&&!live&&!bytes&&!memcmp(&s,&empty,sizeof s));
      assert(!memcmp(&c,&saved,sizeof c)); }

    /* Successful byte hashes are independent of allocation addresses. Filled
     * from the source-matched pre-fix facade control in the private audit. */
    static const uint64_t expected[3] = { UINT64_C(0x749d25f90ca5ba82), UINT64_C(0x5cce56bbc22c00aa), UINT64_C(0xbe006adc59d17b9e) };
    size_t cases = 0;
    for (int cycle = 0; cycle < 100; cycle++) {
        for (int shape = 0; shape < 3; shape++) {
            int w = shape + 1, d = 1 + shape % 2, n = shape ? 2 : 0;
            for (size_t point = 1; point <= 6; point++) {
                start(point); Mesh m = { 0 };
                assert(facade_build(&m, w, d, n) == -1);
                assert(calls >= point); Mesh zero = { 0 }; assert(!memcmp(&m, &zero, sizeof zero));
                mesh_free(&m); mesh_free(&m); assert(!live && !bytes); cases++;
            }
            start(0); Mesh m = { 0 }; assert(!facade_build(&m, w, d, n));
            uint64_t got = facade_hash(&m);
            assert(got == expected[shape]);
            if (!cycle) printf("facade shape=%d hash=%016llx\n", shape, (unsigned long long)got);
            mesh_free(&m); assert(!live && !bytes);
        }
        for (uint32_t count = 0; count <= 7; count += 7) {
            City city = { 0 }; city.bundle.nentries = count;
            start(1); Scene s; memset(&s, 0xCC, sizeof s);
            assert(map_build(&city, "unused", 0, &s) == -1);
            Scene zero = { 0 }; assert(!memcmp(&s, &zero, sizeof zero));
            scene_free(&s); assert(!live && !bytes); cases++;
            start(0); assert(!map_build(&city, "unused", 0, &s));
            for (uint32_t k = 0; k < count; k++) assert(s.mesh_of_entry[k] == -1);
            scene_free(&s); scene_free(&s); assert(!live && !bytes);
        }
    }
    assert(made == released);
    printf("PASS: %zu injected failures, 300 facade and 200 scene-map successes, %zu allocations freed, peak %zu bytes.\n", cases, released, peak);
    return 0;
}
