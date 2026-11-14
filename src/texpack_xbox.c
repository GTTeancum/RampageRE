/* Xbox baseline: draw original decoded game textures. Desktop texture-pack
 * discovery and PNG dumping use host filesystem APIs not present on nxdk.
 * This adapter deliberately leaves replacement packs disabled; it does not
 * alter texture decoding or the renderer's normal upload path.
 *
 * What the Xbox has instead is a replacement by name (xtp_*, below): XBOXTEX.RXT beside RAMPAGE.VOL, made on a PC by
 * tools/xbox_glyphs.py. The game's button pictures are the PS2's (CommonGUI.bdp: Controller_B0, Controller_B1,
 * Controller_Back, Controller_Select, Controller_White, Trigger_Left, Trigger_Right; the names are the studio's own
 * cross-platform ones, and its Xbox build has its A / B / X / Y pictures under the very same names and sizes), so a
 * picture in that file is shown in place of the bundle's texture of the same name. Nothing of the game's art is in the repository: the file is made from the
 * player's own copy of the pictures. */
#include "texpack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#include "data.h"

/* XBOXTEX.RXT: "RXTP", u32 version 1, u32 count, then count x { char name[40] (0-padded), u32 offset from the file's
 * start, u16 width, u16 height }, then each picture's width x height RGBA bytes, top row first (as tex_decode's).
 * Only the table is kept in memory; a picture is read when its texture is decoded. */
#define XTP_MAX 64
typedef struct { char name[40]; uint32_t offset; uint16_t w, h; } XtpEntry;
static struct { char path[300]; XtpEntry e[XTP_MAX]; uint32_t n, size; } xtp;
int g_xbox_glyphs = 1;                   /* --xoff glyphs: the bundles' own pictures */

int xtp_open(const char *vol_path)
{
    xtp.n = 0;
    if (!g_xbox_glyphs || !vol_path)
        return 0;
    const char *slash = strrchr(vol_path, '\\'), *fs = strrchr(vol_path, '/');
    if (!slash || (fs && fs > slash)) slash = fs;
    size_t dir = slash ? (size_t)(slash - vol_path) + 1 : 0;
    if (dir + sizeof "XBOXTEX.RXT" > sizeof xtp.path)
        return 0;
    memcpy(xtp.path, vol_path, dir);
    memcpy(xtp.path + dir, "XBOXTEX.RXT", sizeof "XBOXTEX.RXT");
    FILE *f = fopen(xtp.path, "rb");
    if (!f)
        return 0;
    uint8_t head[12], rec[48];
    uint32_t count = 0;
    if (fread(head, 1, sizeof head, f) == sizeof head && memcmp(head, "RXTP", 4) == 0 && rd32(head + 4) == 1)
        count = rd32(head + 8);
    if (fseek(f, 0, SEEK_END) == 0) {
        long end = ftell(f);
        xtp.size = end > 0 ? (uint32_t)end : 0;
    }
    if (count > XTP_MAX || fseek(f, 12, SEEK_SET) != 0)
        count = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (fread(rec, 1, sizeof rec, f) != sizeof rec)
            break;
        XtpEntry *e = &xtp.e[xtp.n];
        memcpy(e->name, rec, 40);
        e->name[39] = 0;
        e->offset = rd32(rec + 40);
        e->w = rd16(rec + 44);
        e->h = rd16(rec + 46);
        /* a picture the file really holds, of a size a GUI texture has */
        if (!e->name[0] || !e->w || !e->h || e->w > 1024 || e->h > 1024 || e->offset > xtp.size ||
            (uint32_t)e->w * e->h * 4u > xtp.size - e->offset)
            continue;
        xtp.n++;
    }
    fclose(f);
    if (xtp.n)
        printf("Xbox pictures: %u replaced from %s\n", (unsigned)xtp.n, xtp.path);
    return (int)xtp.n;
}

int xtp_find(const char *name)
{
    if (!xtp.n || !name)
        return -1;
    for (uint32_t i = 0; i < xtp.n; i++)
        if (_stricmp(xtp.e[i].name, name) == 0)
            return (int)i;
    return -1;
}

int xtp_decode(int i, uint8_t **rgba, int *w, int *h)
{
    if (i < 0 || (uint32_t)i >= xtp.n)
        return -1;
    const XtpEntry *e = &xtp.e[i];
    size_t bytes = (size_t)e->w * e->h * 4u;
    uint8_t *px = malloc(bytes);
    if (!px)
        return -2;
    FILE *f = fopen(xtp.path, "rb");
    int ok = f && fseek(f, (long)e->offset, SEEK_SET) == 0 && fread(px, 1, bytes, f) == bytes;
    if (f)
        fclose(f);
    if (!ok) {
        free(px);
        return -1;
    }
    *rgba = px;
    *w = e->w;
    *h = e->h;
    return 0;
}

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
