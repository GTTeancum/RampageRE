/* Xbox baseline: draw original decoded game textures. Desktop texture-pack
 * discovery and PNG dumping use host filesystem APIs not present on nxdk.
 * This adapter deliberately leaves replacement packs disabled; it does not
 * alter texture decoding or the renderer's normal upload path. */
#include "texpack.h"
#include <stdio.h>

static char scope[64] = "Common";

uint64_t tp_hash(char kind, const void *data, uint32_t bytes, int w, int h)
{
    uint64_t hash = 0xcbf29ce484222325ull;
    const uint8_t *p = (const uint8_t *)data;
    uint8_t header[9] = {(uint8_t)kind, (uint8_t)w, (uint8_t)(w >> 8),
                         (uint8_t)(w >> 16), (uint8_t)(w >> 24),
                         (uint8_t)h, (uint8_t)(h >> 8),
                         (uint8_t)(h >> 16), (uint8_t)(h >> 24)};
    for (unsigned i = 0; i < sizeof header; ++i) { hash ^= header[i]; hash *= 0x100000001b3ull; }
    for (uint32_t i = 0; i < bytes; ++i) { hash ^= p[i]; hash *= 0x100000001b3ull; }
    return hash ? hash : 1;
}
int tp_active(void) { return 0; }
void tp_scope(const char *name) { snprintf(scope, sizeof scope, "%s", name && *name ? name : "Common"); }
const char *tp_scope_get(void) { return scope; }
void tp_set_dump(int on) { (void)on; }
int tp_replace(uint64_t hash, int mode) { (void)hash; (void)mode; return 0; }
uint8_t *tp_pixels(uint64_t hash, int mode, int *w, int *h)
{ (void)hash; (void)mode; (void)w; (void)h; return NULL; }
void tp_upload(uint8_t *pixels, int w, int h, int mode)
{ (void)pixels; (void)w; (void)h; (void)mode; }
void tp_dump(uint64_t hash, const uint8_t *pixels, int w, int h)
{ (void)hash; (void)pixels; (void)w; (void)h; }
