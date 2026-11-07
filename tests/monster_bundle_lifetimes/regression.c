/* Synthetic ownership providers; actual owning code is in subject.inc. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "city.h"
#include "city-status.inc"
enum { VALID, MISSING_ANIMATION, MISSING_MODEL, MISSING_TREE };
static int mode;
static struct { void *ptr; size_t bytes; } owned[128];
static size_t live_bytes, live_allocs, acquired, released;
static void *tracked_calloc(size_t n, size_t size)
{
    assert(!size || n <= (size_t)-1 / size);
    void *p = calloc(n, size);
    assert(p);
    for (size_t i = 0; i < sizeof owned / sizeof *owned; i++)
        if (!owned[i].ptr) {
            owned[i].ptr = p; owned[i].bytes = n * size;
            live_bytes += n * size; live_allocs++; acquired++;
            return p;
        }
    abort();
}
static void tracked_free(void *p)
{
    if (!p) return;
    for (size_t i = 0; i < sizeof owned / sizeof *owned; i++)
        if (owned[i].ptr == p) {
            live_bytes -= owned[i].bytes; live_allocs--; released++;
            owned[i].ptr = NULL; owned[i].bytes = 0; free(p); return;
        }
    assert(!"Unknown or double-freed pointer");
}
#define calloc tracked_calloc
#define free tracked_free
const VolFile *vol_find(const Vol *vol, const char *file)
{
    (void)vol;
    static VolFile entry;
    int animation = strstr(file, "_anims") != NULL;
    return (mode == MISSING_ANIMATION && animation) || (mode == MISSING_MODEL && !animation) ? NULL : &entry;
}
uint8_t *vol_read(const Vol *vol, const char *file, uint32_t *size)
{
    (void)vol;
    int animation = strstr(file, "_anims") != NULL;
    if ((mode == MISSING_ANIMATION && animation) || (mode == MISSING_MODEL && !animation))
        return NULL;
    *size = 16;
    return calloc(*size, 1);
}
int bdp_load(Bundle *b, uint8_t *file, uint32_t size)
{
    b->file = file; b->file_size = size;
    if (mode != MISSING_TREE) {
        b->entries = calloc(1, sizeof *b->entries);
        b->nentries = 1; b->entries[0].type = ET_TREE; b->entries[0].extra = 1;
    }
    return 0;
}
void bdp_free(Bundle *b)
{
    free(b->file); free(b->entries); memset(b, 0, sizeof *b);
}
#include "subject.inc"

static void cleanup(Scene *s)
{
    while (s->nown) {
        Bundle *b = s->own[--s->nown]; s->own[s->nown] = NULL;
        bdp_free(b); free(b);
    }
}
int main(void)
{
    assert_city_status_contract();
    City c = {0};
    size_t cases = 0, failures = 0, admissions = 0, fatals = 0;
    const int cap = (int)(sizeof ((Scene *)0)->own / sizeof * ((Scene *)0)->own);
    for (int prior = 0; prior <= cap; prior++)
        for (int missing = MISSING_ANIMATION; missing <= MISSING_TREE; missing++) {
            Scene s = {0};
            Bundle *before[2 * SCENE_MONSTERS] = {0};
            Bundle snapshots[2 * SCENE_MONSTERS] = {0};
            mode = VALID;
            for (int i = 0; i < prior; i++) {
                before[i] = own_bundle(NULL, &s, "prior.cmp");
                assert(before[i]); snapshots[i] = *before[i];
            }
            size_t bytes = live_bytes, allocs = live_allocs;
            mode = missing;
            for (int retry = 0; retry < 10; retry++) {
                int result = city_monster(&c, &s, "monster", -1, 0, 0, 0);
                int capacity_failure = prior == cap || (prior == cap - 1 && missing == MISSING_TREE);
                if (capacity_failure) {
                    assert(result == -2 && s.construction_failed);
                    assert(s.nown == cap && s.nmon == 0 && s.ninstances == 0);
                    size_t stable_bytes = live_bytes, stable_allocs = live_allocs, stable_acquired = acquired;
                    assert(city_monster(&c, &s, "monster", -1, 0, 0, 0) == -2);
                    assert(live_bytes == stable_bytes && live_allocs == stable_allocs && acquired == stable_acquired);
                    fatals++;
                    break;                          /* required failure permits teardown, not another admission */
                }
                assert(result == -1 && !s.construction_failed);
                assert(s.nown == prior && s.nmon == 0 && s.ninstances == 0);
                assert(live_bytes == bytes && live_allocs == allocs);
                for (int i = 0; i < prior; i++) {
                    assert(s.own[i] == before[i]);
                    assert(!memcmp(s.own[i], &snapshots[i], sizeof snapshots[i]));
                }
                for (int i = prior; i < cap; i++) assert(s.own[i] == NULL);
                failures++;
            }
            mode = VALID;
            size_t before_retry = acquired;
            int was_failed = s.construction_failed;
            int r = city_monster(&c, &s, "monster", -1, 0, 0, 0);
            if (prior <= cap - 2) {
                assert(r == 777 && s.nown == prior + 2); admissions++;
            } else {
                assert(r == -2 && s.construction_failed && s.nown == cap);
                if (was_failed) assert(acquired == before_retry);
                else fatals++;
            }
            for (int i = 0; i < prior; i++) {
                assert(s.own[i] == before[i]);
                assert(!memcmp(s.own[i], &snapshots[i], sizeof snapshots[i]));
            }
            assert(s.nmon == 0 && s.ninstances == 0);
            cleanup(&s);
            assert(live_bytes == 0 && live_allocs == 0 && acquired == released);
            cases++;
        }
    /* The monster-count rejection occurs before any read or allocation. */
    Scene full = {0}; full.nmon = SCENE_MONSTERS;
    size_t old_acquired = acquired;
    assert(city_monster(&c, &full, "monster", -1, 0, 0, 0) == -1);
    assert(acquired == old_acquired && live_allocs == 0);
    printf("PASS cases=%zu failed_retries=%zu valid_prerequisite_admissions=%zu capacity_fatals=%zu allocations=%zu releases=%zu live_allocs=%zu live_bytes=%zu\n",
           cases, failures, admissions, fatals, acquired, released, live_allocs, live_bytes);
    return 0;
}
