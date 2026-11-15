/* bundle.c - Pipeworks bundles (.bdp/.cmp), read by the game at 0x1A8740 (tools/bdp.py, docs/FORMAT_BDP.md).
 * Header: "Pipeworks Bundle File version 1.3", 0x30 version 1300, 0x34 offset of the name table, 0x38 u32[10]
 * segment headers. Segment 0 holds every entry: u16 number, u16 count, u32 entry table, u32 data offset / size,
 * u32 data2 offset / size, then 0x12 bytes per record (u16 entry, u32 data offset, size, data2 offset, size).
 * The Wii's (.bdg / .cmg, "version 1.4", 1400) are the same, big endian but for the name table. */
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#include "data.h"

static const char MAGIC[] = "Pipeworks Bundle File version 1.";   /* 3 the PS2's, 4 the Wii's */

/* ---- the Wii's entries into the PS2's layout (docs/FORMAT_BDP.md "The Wii's bundles"). A map marks where each
 * field starts and how wide it is (4, 2; 0 = a byte or inside a field), then every marked field is swapped. The
 * fields are the ones the port reads (tree.c, anim.c, mesh.c's materials, moves.c, director.c); the rest of an entry
 * is left as it is */
typedef struct { uint8_t *w; uint32_t n; } Map;

static void mark(Map *m, uint32_t from, uint32_t to, uint8_t width)   /* [from, to) as fields of width (0: bytes) */
{
    if (to > m->n)
        to = m->n;
    for (uint32_t i = from; i < to; i++)
        m->w[i] = width && (i - from) % width == 0 && i + width <= to ? width : 0;
}

static void swap_marked(uint8_t *d, const Map *m)
{
    for (uint32_t i = 0; i < m->n; i++)
        if (m->w[i] == 4) {
            uint8_t t = d[i]; d[i] = d[i + 3]; d[i + 3] = t;
            t = d[i + 1]; d[i + 1] = d[i + 2]; d[i + 2] = t;
        } else if (m->w[i] == 2) {
            uint8_t t = d[i]; d[i] = d[i + 1]; d[i + 1] = t;
        }
}

static uint32_t be32_at(const Map *m, const uint8_t *d, uint32_t o) { return o + 4 <= m->n ? rd32be(d + o) : 0; }
static uint32_t be16_at(const Map *m, const uint8_t *d, uint32_t o) { return o + 2 <= m->n ? rd16be(d + o) : 0; }

/* node tree: the header words, the nodes, the node and by-name tables and the bind pose 32-bit; the mirror table
 * u16 (tools/anim.py Skeleton) */
static void map_tree(Map *m, const uint8_t *d)
{
    uint32_t first = be32_at(m, d, 0x1C), n = be32_at(m, d, 0x20), mt = be32_at(m, d, 0x38);
    mark(m, 0x1C, m->n, 4);
    if (mt && first >= 0x3C && n < 0x10000)
        mark(m, mt, mt + ((2 * n + 3) & ~3u), 2);
}

/* clip: KeyInterpAnim (kind 2) its tracks walked, keys u16 / s16 and the bytes of the track heads; RawAnim (kind 1)
 * all 32-bit; the skeleton's name index u16 (tools/anim.py Animation, RawAnim) */
static void map_clip(Map *m, const uint8_t *d)
{
    uint32_t kind = be32_at(m, d, 0x20);
    mark(m, 0x1C, 0x28, 4);
    mark(m, 0x28, 0x2C, 2);
    if (kind == 1) {
        mark(m, 0x2C, m->n, 4);
        return;
    }
    if (kind != 2)
        return;
    mark(m, 0x2C, 0x44, 4);
    uint32_t bones = be32_at(m, d, 0x2C), mask = be32_at(m, d, 0x34);
    uint32_t at = be32_at(m, d, 0x38), rot = be32_at(m, d, 0x3C);
    mark(m, mask, at, 4);                                /* the channel mask: u32 words */
    for (uint32_t i = 0; i < bones && at + 0x10 <= m->n; i++) {
        uint32_t keys = d[at + 0xD];
        mark(m, at, at + 12, 4);                         /* f32[3] scale, then u8 bone, count, count - 1, 0 */
        mark(m, at + 12, at + 16, 0);
        mark(m, at + 16, at + 16 + 8 * keys, 2);         /* u16 time, s16 x, y, z */
        at += 16 + 8 * keys;
    }
    for (uint32_t i = 0; i < bones && rot + 2 <= m->n; i++) {
        uint32_t keys = d[rot + 1];
        mark(m, rot + 2, rot + 2 + 8 * keys, 2);         /* u8 bone, count, then u16 time, s16 a, b, c */
        rot += 2 + 8 * keys;
    }
}

/* material (0x70 bytes on the Wii): u16 own name, u8 kind, u8, u32 flags, f32 colours, u16 texture name indices by
 * kind, then with flag 0x100 the region's four floats (mesh.c material_texture .. material_region) */
static void map_material(Map *m, const uint8_t *d)
{
    static const uint8_t textures[9] = { 0, 0, 1, 2, 2, 2, 3, 2, 2 };
    static const uint8_t region[9] = { 0, 0, 0x3A, 0x3C, 0x3C, 0x3C, 0x3E, 0x3C, 0x3E };
    if (m->n < 0x38)
        return;
    uint8_t kind = d[2] <= 8 ? d[2] : 0;
    mark(m, 0, 2, 2);
    mark(m, 4, 0x38, 4);
    mark(m, 0x38, 0x38 + 2u * textures[kind], 2);
    if (be32_at(m, d, 4) & 0x100 && region[kind])
        mark(m, region[kind], region[kind] + 16u, 4);
}

/* cloth (tools/anim.py Cloth, cloth.c, tree.c cloth_particles): 32-bit but each part's flag bytes (+0x50 .. +0x57)
 * and the particles. A Wii particle is 72 bytes, the PS2's 64 with 8 bytes 0xEA after the normal: each is moved
 * into the PS2's 64 where it starts (the readers find every array by its offset, so the room left at the end of a
 * part's particles is never read) and swapped there: f32 position, normal, value, weights; u16 bones; u8 count,
 * 0xEA; u16 held; u32 0xFFFFFFFF x 2 */
static void map_cloth(Map *m, uint8_t *d)
{
    uint32_t nparts = be32_at(m, d, 0x34), parts = be32_at(m, d, 0x38);
    mark(m, 0x1C, m->n, 4);
    for (uint32_t k = 0; k < nparts && k < 4096 && parts + 0x8C * (k + 1) <= m->n; k++) {
        uint32_t r = parts + 0x8C * k, count = be32_at(m, d, r + 0x84), at = be32_at(m, d, r + 0x88);
        mark(m, r + 0x50, r + 0x58, 0);
        if (count > 65536 || (uint64_t)at + 72ull * count > m->n)
            continue;
        mark(m, at, at + 72 * count, 0);                 /* swapped below, once moved */
        for (uint32_t i = 0; i < count; i++) {
            uint8_t *dst = d + at + 64 * i;
            const uint8_t *src = d + at + 72 * i;
            memmove(dst, src, 0x18);
            memmove(dst + 0x18, src + 0x20, 0x28);
            Map one = { (uint8_t[64]){ 0 }, 64 };
            mark(&one, 0, 0x2C, 4);
            mark(&one, 0x2C, 0x34, 2);
            mark(&one, 0x36, 0x38, 2);
            mark(&one, 0x38, 0x40, 4);
            swap_marked(dst, &one);
        }
    }
}

/* property book (tools/prb.py, prb.c): the header's count, size and group table, each group's five words and its
 * records {kind, name, value} 32-bit, but a record's kind is bytes on the Wii (u8 type, u8 form, u16 length of
 * "Type"), as the PS2's little endian u32 reads it; the groups found from the table and every group-valued record */
static int map_book(Map *m, const uint8_t *d)
{
    uint32_t count = be32_at(m, d, 0x1C), table = be32_at(m, d, 0x24);
    if (count > 65536)
        return 0;
    mark(m, 0x1C, 0x28, 4);
    mark(m, table, table + 4 * count, 4);
    uint32_t cap = count + 64, n = 0, *todo = malloc(sizeof *todo * cap);
    uint8_t *seen = calloc(m->n / 4 + 1, 1);
    if (!todo || !seen)
        goto failed;
    for (uint32_t i = 0; todo && i < count; i++)
        todo[n++] = be32_at(m, d, table + 4 * i);
    while (todo && seen && n) {
        uint32_t g = todo[--n], records = be32_at(m, d, g + 8);
        if (g % 4 || g + 0x14 > m->n || seen[g / 4] || records > 65536 || g + 0x14 + 12ull * records > m->n)
            continue;
        seen[g / 4] = 1;
        mark(m, g, g + 0x14, 4);
        for (uint32_t r = 0; r < records; r++) {
            uint32_t at = g + 0x14 + 12 * r;
            mark(m, at, at + 2, 0);
            mark(m, at + 2, at + 4, 2);
            mark(m, at + 4, at + 12, 4);
            if (d[at] == PK_GROUP) {
                if (n == cap) {
                    if (cap > UINT32_MAX / 2 || (uint64_t)cap * 2 > SIZE_MAX / sizeof *todo)
                        goto failed;
                    uint32_t next = cap * 2;
                    uint32_t *grown = realloc(todo, sizeof *todo * next);
                    if (!grown)
                        goto failed;
                    todo = grown;
                    cap = next;
                }
                todo[n++] = g + be32_at(m, d, at + 8);
            }
        }
    }
    free(todo);
    free(seen);
    return 0;
failed:
    free(todo);
    free(seen);
    return -1;
}

/* string table (tools/loc.py, hud.c and shell.c loc_find): the header from 0x1C (u32 4, languages, count, the 11
 * columns' offsets) and every column of string offsets 32-bit; the strings themselves as they are */
static void map_loc(Map *m, const uint8_t *d)
{
    uint32_t count = be32_at(m, d, 0x24);
    mark(m, 0x1C, 0x54, 4);
    for (uint32_t c = 0; c < 11 && count < 65536; c++) {
        uint32_t at = be32_at(m, d, 0x28 + 4 * c);
        if (at)
            mark(m, at, at + 4 * count, 4);
    }
}

/* texture (0x68 bytes on the Wii): u32 kind, u32 GX format, u16 width, height, mip levels, 0xFFFF; its pixels
 * (data2) stay as they are, tex.c reads them as GX tiles */
static void map_texture(Map *m)
{
    mark(m, 0, 8, 4);
    mark(m, 8, 0x10, 2);
}

/* MONSTER_DATA (tools/monster.py; the fields moves.c and director.c read): 32-bit but for the u16 coded item
 * offsets, the attack items' parts and count (+0x8 .. +0x13) and the effect items' slot, effect, on, flags (+0x8 ..
 * +0xF), each state's +0xC / +0xE and its 16 bytes at +0x18, each transition's priority and count (+0x1C, +0x1E),
 * each state node's clip events (u16 count, first at +0x28), u8 +0x60, +0x61 and u16 +0x62 .. +0x66 (found by
 * matching George's PS2 and Wii records field by field), the attachment nodes' bone and index (+0x34), the shapes'
 * bone and part (+0x14, +0x16), the body parts' count
 * and bones (+0xC ..), the effect nodes' bone and mirror (+0x30, +0x32); the bytes after the states (the next-hop
 * table) as they are */
static void map_monster(Map *m, const uint8_t *d)
{
    if (m->n < 0xAA8 || rd32be(d) != 11)
        return;
    uint32_t graph = be32_at(m, d, 0xA54), nstates = be32_at(m, d, graph + 0x2C), at = be32_at(m, d, graph + 0x30);
    mark(m, 0, at < m->n ? at : m->n, 4);
    uint32_t nitems = be32_at(m, d, 0xA60), block = be32_at(m, d, 0xA64), offsets = be32_at(m, d, 0xA68);
    for (uint32_t i = 0; i < nitems && offsets + 2 * i + 2 <= m->n; i++) {
        uint32_t o = block + be16_at(m, d, offsets + 2 * i), type = be32_at(m, d, o);
        if (type == 0)
            mark(m, o + 8, o + 0x14, 2);
        else if (type == 12)
            mark(m, o + 8, o + 0x10, 2);
    }
    mark(m, offsets, offsets + 2 * nitems, 2);
    uint32_t nnodes = be32_at(m, d, 0xA4C), nodes = be32_at(m, d, 0xA50);
    for (uint32_t k = 0; k < nnodes && k < 4096; k++) {
        uint32_t nd = nodes + 0x84 * k;
        mark(m, nd + 0x28, nd + 0x2C, 2);                /* the clip events: u16 count, u16 first */
        mark(m, nd + 0x60, nd + 0x62, 0);
        mark(m, nd + 0x62, nd + 0x68, 2);
    }
    uint32_t natt = be32_at(m, d, 0xA84), att = be32_at(m, d, 0xA88);
    for (uint32_t k = 0; k < natt && k < 4096; k++)      /* attachment nodes: u16 bone, u16 index at +0x34 */
        mark(m, att + 0x38 * k + 0x34, att + 0x38 * k + 0x38, 2);
    uint32_t nsh = be32_at(m, d, 0xA44), sh = be32_at(m, d, 0xA48);
    for (uint32_t k = 0; k < nsh && k < 4096; k++)
        mark(m, sh + 24 * k + 0x14, sh + 24 * k + 0x18, 2);
    uint32_t nparts = be32_at(m, d, 0xA6C), parts = be32_at(m, d, 0xA74);
    for (uint32_t k = 0; k < nparts && k < 4096; k++) {
        uint32_t o = be32_at(m, d, parts + 4 * k);
        mark(m, o + 0xC, o + 0xE + 2 * be16_at(m, d, o + 0xC), 2);
    }
    uint32_t nsets = be32_at(m, d, 0xA78), sets = be32_at(m, d, 0xA80);
    for (uint32_t k = 0; k < nsets && k < 4096; k++) {
        uint32_t rec = be32_at(m, d, sets + 4 * k), cnt = be32_at(m, d, rec), arr = be32_at(m, d, rec + 4);
        for (uint32_t j = 0; j < cnt && j < 4096; j++)
            mark(m, arr + 0x34 * j + 0x30, arr + 0x34 * j + 0x34, 2);
    }
    for (uint32_t k = 0; k < nstates && k < 4096 && at + 0x2C <= m->n; k++) {
        uint32_t ntrans = be16_at(m, d, at + 0xE);
        mark(m, at, at + 0x2C, 4);
        mark(m, at + 0xC, at + 0x10, 2);
        mark(m, at + 0x18, at + 0x28, 0);
        for (uint32_t t = 0; t < ntrans; t++) {
            uint32_t q = at + 0x2C + 0x74 * t;
            mark(m, q, q + 0x74, 4);
            mark(m, q + 0x1C, q + 0x20, 2);
        }
        at += 0x2C + 0x74 * ntrans;
    }
}

static int swap_entry(const Bundle *b, BdpEntry *e)
{
    const char *name = bdp_name(b, e->name);
    size_t len = strlen(name);
    int monster = e->type == ET_RAW && len >= 12 && strcmp(name + len - 12, "MONSTER_DATA") == 0;
    if (!e->data || (e->type != ET_TREE && e->type != ET_ANIM && e->type != ET_MATERIAL && e->type != ET_TEXTURE &&
                     e->type != ET_CLOTH && e->type != ET_BOOK && e->type != ET_LOC && !monster))
        return 0;                                        /* raw files (scripts, fonts, .ifl) stay little endian */
    uint8_t *d = (uint8_t *)e->data;
    Map m = { calloc(e->size ? e->size : 1, 1), e->size };
    if (!m.w)
        return -1;
    if (e->type == ET_TREE) map_tree(&m, d);
    else if (e->type == ET_ANIM) map_clip(&m, d);
    else if (e->type == ET_MATERIAL) map_material(&m, d);
    else if (e->type == ET_TEXTURE) map_texture(&m);
    else if (e->type == ET_CLOTH) map_cloth(&m, d);
    else if (e->type == ET_BOOK) {
        if (map_book(&m, d) != 0) {
            free(m.w);
            return -1;
        }
    }
    else if (e->type == ET_LOC) map_loc(&m, d);
    else map_monster(&m, d);
    swap_marked(d, &m);
    free(m.w);
    return 0;
}

/* a lean bundle's payload area (bdp_open): what of it is in memory. The runs are the kept stretches of the file's
 * payload area in file order, each at its own place in the image; the textures' pixels are in none of them */
typedef struct { uint32_t from, to, at; } LeanRun;       /* [from, to) from the payload area's start; at: from file */
typedef struct { const LeanRun *run; uint32_t n; } Lean;

static const uint8_t *lean_at(const Lean *lm, const uint8_t *file, uint32_t off, uint32_t size)
{
    uint32_t lo = 0, hi = lm->n;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (off >= lm->run[mid].to) lo = mid + 1; else hi = mid;
    }
    if (lo == lm->n || off < lm->run[lo].from || size > lm->run[lo].to - off)
        return NULL;
    return file + lm->run[lo].at + (off - lm->run[lo].from);
}

#ifdef RAMPAGE_XBOX
static void name_table(Bundle *b);
#endif

static int load(Bundle *b, uint8_t *file, uint32_t size, const Lean *lm)
{
    memset(b, 0, sizeof *b);
    b->file = file;
    b->file_size = size;
    if (size < 0x60 || memcmp(file, MAGIC, sizeof MAGIC - 1) != 0)
        goto failed;
    b->wii = file[sizeof MAGIC - 1] == '4';
    b->ps2_mesh = b->wii && file[0x2B] == 'P';           /* (tools/wii2ps2.py models: its rigid meshes are the PS2's) */
    uint32_t (*r32)(const uint8_t *) = b->wii ? rd32be : rd32;
    uint16_t (*r16)(const uint8_t *) = b->wii ? rd16be : rd16;
    if ((file[sizeof MAGIC - 1] != '3' && !b->wii) || r32(file + 0x30) != (b->wii ? 1400u : 1300u))
        goto failed;
    uint32_t names_at = r32(file + 0x34);                /* the name table: little endian on the Wii too */
    b->nnames = rd32(file + names_at);
    b->names = calloc(b->nnames ? b->nnames : 1, sizeof *b->names);
    if (!b->names)
        goto failed;
    for (uint32_t i = 0; i < b->nnames; i++)
        b->names[i] = (const char *)file + names_at + rd32(file + names_at + 4 + 4 * i);
    const uint8_t *seg = file + r32(file + 0x38);
    uint32_t count = r16(seg + 2), table = r32(seg + 4), d_off = r32(seg + 8), d2_off = r32(seg + 16);
    b->entries = calloc(count ? count : 1, sizeof *b->entries);
    b->by_name = malloc(sizeof *b->by_name * (b->nnames ? b->nnames : 1));
    b->mat_by_name = malloc(sizeof *b->mat_by_name * (b->nnames ? b->nnames : 1));
    b->tex_by_name = malloc(sizeof *b->tex_by_name * (b->nnames ? b->nnames : 1));
    if (!b->entries || !b->by_name || !b->mat_by_name || !b->tex_by_name)
        goto failed;
    for (uint32_t i = 0; i < b->nnames; i++)
        b->by_name[i] = b->mat_by_name[i] = b->tex_by_name[i] = -1;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *rec = seg + 0x18 + 0x12 * i, *ent = file + table + 16 * i;
        BdpEntry *e = &b->entries[i];
        e->index = (uint16_t)i;
        e->wii = (uint8_t)b->wii;
        e->type = ent[2];
        e->extra = ent[3];
        e->name = r32(ent + 4);
        e->size = r32(rec + 6);
        e->size2 = r32(rec + 14);
        e->data = e->size ? file + d_off + r32(rec + 2) : NULL;
        e->data2 = e->size2 ? file + d2_off + r32(rec + 10) : NULL;
        if (lm && e->size2) {                            /* a lean image: the payload is where the map says, or on
                                                          * the disc alone (a texture's pixels: tex_decode) */
            e->data2 = lean_at(lm, file, r32(rec + 10), e->size2);
            if (!e->data2) {
                if (e->type != ET_TEXTURE && e->type != ET_MESH && e->type != ET_SKINNED)
                    goto failed;
                e->at2 = d2_off + r32(rec + 10);
            }
        }
        e->owner = b;
        if (e->name < b->nnames) {
            b->by_name[e->name] = (int32_t)i;
            if (e->type == ET_MATERIAL) b->mat_by_name[e->name] = (int32_t)i;
            if (e->type == ET_TEXTURE) b->tex_by_name[e->name] = (int32_t)i;
        }
    }
    b->nentries = count;
#ifdef RAMPAGE_XBOX
    name_table(b);
#endif
    if (b->wii)
        for (uint32_t i = 0; i < count; i++)
            if (swap_entry(b, &b->entries[i]) != 0)
                goto failed;
    return 0;
failed:
    /* The caller retains its input on failure, including a partially converted
     * Wii buffer. Only metadata allocated by this call is released here. */
    b->file = NULL;
    bdp_free(b);
    return -1;
}

int bdp_load(Bundle *b, uint8_t *file, uint32_t size) { return load(b, file, size, NULL); }

#ifdef RAMPAGE_XBOX
int g_xbox_tex_drop = 1;                 /* bundles are read without their textures' pixels (--xoff texdrop: whole) */
int g_xbox_mesh_drop = 1;                /* and without their meshes' vertex streams (--xoff meshdrop: those kept) */

static int lean_cmp(const void *a, const void *b)
{
    const LeanRun *x = a, *y = b;
    return x->from < y->from ? -1 : x->from > y->from;
}

/* The Xbox's 64 MB: a bundle's image without its textures' pixels, which the PS2 kept in the file for its GS and
 * which here are only ever read to make a GPU texture (tex_decode), a second copy the console has no room for
 * (2.8 of Las Vegas's 5.2 MB, 34 of the bundles' 121 MB). Every one of the game's 117 bundles is laid out with all
 * its tables, names and entry data below the payload area and the payloads last (checked over the whole VOL), so
 * the image is that lower part followed by the payloads that are kept (meshes, palettes, cloth), packed in file
 * order; a texture's pixels are read from the disc when it is decoded (BdpEntry.at2). Anything unexpected returns
 * -1 and the caller reads the whole file as before.
 * The Wii folder's bundles (1.4) the same way: they are laid out alike, their numbers big endian (all 129 of the
 * Wii's checked: tables, names and entry data below the payload area). They need it more: Dallas.bdg is 9.4 MB,
 * 5.2 of it textures and 2.8 mesh streams; Shell.bdg 12 MB, 11.7 of it textures. */
static int lean_open(Bundle *b, const Vol *v, const VolFile *f)
{
    uint8_t head[0x60], sh[0x18];
    if (f->size < sizeof head || vol_read_range(v, f, 0, head, sizeof head) != 0 ||
        memcmp(head, MAGIC, sizeof MAGIC - 1) != 0)
        return -1;
    int wii = head[sizeof MAGIC - 1] == '4';
    uint32_t (*r32)(const uint8_t *) = wii ? rd32be : rd32;
    uint16_t (*r16)(const uint8_t *) = wii ? rd16be : rd16;
    if ((head[sizeof MAGIC - 1] != '3' && !wii) || r32(head + 0x30) != (wii ? 1400u : 1300u))
        return -1;
    uint32_t seg = r32(head + 0x38);
    if (seg > f->size - sizeof sh || vol_read_range(v, f, seg, sh, sizeof sh) != 0)
        return -1;
    uint32_t count = r16(sh + 2), table = r32(sh + 4), d_off = r32(sh + 8), d2_off = r32(sh + 16);
    if (d2_off < sizeof head || d2_off > f->size || seg > d2_off || 0x18 + 0x12 * (uint64_t)count > d2_off - seg ||
        table > d2_off || 16 * (uint64_t)count > d2_off - table || d_off > d2_off || r32(head + 0x34) >= d2_off)
        return -1;
    uint32_t area = f->size - d2_off;                    /* the payload area's size */
    uint8_t *low = malloc(d2_off);
    LeanRun *run = malloc((count ? count : 1) * sizeof *run);
    int result = -1;
    if (!low || !run || vol_read_range(v, f, 0, low, d2_off) != 0)
        goto done;
    /* the name table and every entry's data must be in the lower part: the entry loop reads them from it (the
     * table itself is little endian on the Wii too) */
    uint32_t names_at = r32(low + 0x34);
    if (d2_off - names_at < 4 || rd32(low + names_at) > (d2_off - names_at - 4) / 4)
        goto done;
    for (uint32_t i = 0, nn = rd32(low + names_at); i < nn; i++) {
        uint32_t at = rd32(low + names_at + 4 + 4 * i);
        if (at >= d2_off - names_at || !memchr(low + names_at + at, 0, d2_off - names_at - at))
            goto done;
    }
    uint32_t n = 0, textures = 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *rec = low + seg + 0x18 + 0x12 * i, *ent = low + table + 16 * i;
        uint32_t o1 = r32(rec + 2), s1 = r32(rec + 6), o2 = r32(rec + 10), s2 = r32(rec + 14);
        if (s1 && (o1 > d2_off - d_off || s1 > d2_off - d_off - o1))
            goto done;
        if (!s2)
            continue;
        if (o2 > area || s2 > area - o2)
            goto done;
        /* Left on the disc: the textures' pixels, and the meshes' vertex streams, which are parsed into arrays of
         * their own when a level is built (mesh_read) and not looked at again until the next one is */
        if (ent[2] == ET_TEXTURE || (g_xbox_mesh_drop && (ent[2] == ET_MESH || ent[2] == ET_SKINNED))) {
            textures += s2;
            continue;
        }
        run[n].from = o2; run[n].to = o2 + s2; run[n].at = 0;
        n++;
    }
    if (textures < 64u * 1024u)                          /* nothing worth the second read */
        goto done;
    qsort(run, n, sizeof *run, lean_cmp);
    uint32_t m = 0, kept = 0;                            /* neighbours (and the few bytes of padding between them) as
                                                          * one run; a run starts at its file offset modulo 16, as
                                                          * the whole image would have it (the meshes' streams) */
    for (uint32_t i = 0; i < n; i++) {
        if (m && run[i].from <= run[m - 1].to + 16) {
            if (run[i].to > run[m - 1].to) run[m - 1].to = run[i].to;
        } else
            run[m++] = run[i];
    }
    for (uint32_t i = 0; i < m; i++) {
        uint32_t at = d2_off + kept;
        at += ((d2_off + run[i].from) - at) & 15u;
        run[i].at = at;
        kept = at + (run[i].to - run[i].from) - d2_off;
    }
    uint8_t *image = realloc(low, d2_off + (kept ? kept : 1));
    if (!image)
        goto done;
    low = image;
    for (uint32_t i = 0; i < m; i++)
        if (vol_read_range(v, f, d2_off + run[i].from, low + run[i].at, run[i].to - run[i].from) != 0)
            goto done;
    Lean lm = { run, m };
    if (load(b, low, d2_off + kept, &lm) != 0)
        goto done;
    b->src = v;
    b->src_file = *f;
    low = NULL;                                          /* the bundle's now */
    result = 0;
done:
    free(low);
    free(run);
    return result;
}
#endif

static struct { const BdpEntry *e; uint8_t *mem; } payload_slot;

const uint8_t *bdp_payload(const BdpEntry *e)
{
    if (!e || e->data2 || !e->at2 || !e->size2)
        return e ? e->data2 : NULL;
    if (payload_slot.e == e && payload_slot.mem)
        return payload_slot.mem;
    const Bundle *b = e->owner;
    free(payload_slot.mem);
    payload_slot.mem = NULL;
    payload_slot.e = NULL;
    if (!b || !b->src || e < b->entries || e >= b->entries + b->nentries)
        return NULL;
    uint8_t *mem = malloc(e->size2);
    if (!mem)
        return NULL;
    if (vol_read_range(b->src, &b->src_file, e->at2, mem, e->size2) != 0) {
        free(mem);
        return NULL;
    }
    XHITCH(meshes, 1);
    payload_slot.mem = mem;
    payload_slot.e = e;
    return mem;
}

void bdp_payload_release(void)
{
    free(payload_slot.mem);
    payload_slot.mem = NULL;
    payload_slot.e = NULL;
}

int bdp_open(Bundle *b, const Vol *v, const char *name)
{
    memset(b, 0, sizeof *b);
#ifdef RAMPAGE_XBOX
    const VolFile *f = g_xbox_tex_drop && v ? vol_find(v, name) : NULL;
    if (f && lean_open(b, v, f) == 0)                    /* (the VOL's, or a file of the Wii folder) */
        return 0;
    memset(b, 0, sizeof *b);
#endif
    uint32_t size = 0;
    uint8_t *data = vol_read(v, name, &size);
    if (!data)
        return -1;
    if (bdp_load(b, data, size) != 0) {
        free(data);
        return -1;
    }
    return 0;
}

void bdp_free(Bundle *b)
{
    if (payload_slot.e && b->entries && payload_slot.e >= b->entries && payload_slot.e < b->entries + b->nentries)
        bdp_payload_release();                           /* (the buffer is one of this bundle's entries') */
#ifdef RAMPAGE_XBOX
    prb_forget();                        /* remembered property lookups may point into this bundle's books */
#endif
    free(b->file);
    free((void *)b->names);
    free(b->entries);
    free(b->by_name);
    free(b->mat_by_name);
    free(b->tex_by_name);
    free(b->name_slot);
    memset(b, 0, sizeof *b);
}

const char *bdp_name(const Bundle *b, uint32_t i)
{
    if (i >= b->nnames)
        return "";
    const char *s = b->names[i];
    return *s == 0x13 ? s + 1 : s;
}

const BdpEntry *bdp_entry(const Bundle *b, uint32_t name_index)
{
    if (name_index >= b->nnames || b->by_name[name_index] < 0)
        return NULL;
    return &b->entries[b->by_name[name_index]];
}

const BdpEntry *bdp_entry_of(const Bundle *b, uint32_t name_index, int type)
{
    if (name_index >= b->nnames)
        return NULL;
    if (type == ET_MATERIAL || type == ET_TEXTURE) {
        int32_t i = (type == ET_MATERIAL ? b->mat_by_name : b->tex_by_name)[name_index];
        return i >= 0 ? &b->entries[i] : NULL;
    }
    for (uint32_t i = 0; i < b->nentries; i++)
        if (b->entries[i].name == name_index && b->entries[i].type == type)
            return &b->entries[i];
    return NULL;
}

#ifdef RAMPAGE_XBOX
/* bdp_find_name compared the name with every name of the bundle (Las Vegas has thousands), and the shadows, effects
 * and HUD ask by name every frame. The same answer from a table made at the load: the first name that matches,
 * letter case ignored. */
static uint32_t name_hash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        h = (h ^ c) * 16777619u;
    }
    return h;
}

static void name_table(Bundle *b)
{
    uint32_t size = 16;
    while (size < 2 * b->nnames)
        size *= 2;
    b->name_slot = b->nnames ? calloc(size, sizeof *b->name_slot) : NULL;
    if (!b->name_slot)
        return;
    b->name_mask = size - 1;
    for (uint32_t i = 0; i < b->nnames; i++) {
        const char *nm = bdp_name(b, i);
        uint32_t at = name_hash(nm) & b->name_mask;
        while (b->name_slot[at] && _stricmp(bdp_name(b, b->name_slot[at] - 1), nm) != 0)
            at = (at + 1) & b->name_mask;
        if (!b->name_slot[at])                           /* (a name there twice: the first one answers, as before) */
            b->name_slot[at] = i + 1;
    }
}
#endif

int bdp_find_name(const Bundle *b, const char *name)
{
#ifdef RAMPAGE_XBOX
    if (b->name_slot) {
        for (uint32_t at = name_hash(name) & b->name_mask; b->name_slot[at]; at = (at + 1) & b->name_mask)
            if (_stricmp(bdp_name(b, b->name_slot[at] - 1), name) == 0)
                return (int)(b->name_slot[at] - 1);
        return -1;
    }
#endif
    for (uint32_t i = 0; i < b->nnames; i++)
        if (_stricmp(bdp_name(b, i), name) == 0)
            return (int)i;
    return -1;
}
