/* tex.c - textures (bundle entry type 9) to RGBA8 (tools/tex.py). Header: u32 kind, u8 GS pixel format (0x00 PSMCT32,
 * 0x02 PSMCT16, 0x13 PSMT8), u8 flags at 0x07 (0x40 = stored in GS memory order), u16 width, u16 height, ..., u32 name
 * index of the palette at 0x20. Pixels (mip 0 first) are the entry's data2. Palettes (type 13): 256 RGBA in GS CSM1 order,
 * alpha 0x80 = opaque; "_Alpha8" and "_Intensity8" are the engine's own. The Wii's (Bundle.wii) are GX: gx_decode. */
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include "plat.h"
#include "data.h"

/* PSMT8 pixels written to GS memory as PSMCT32 back to rows */
static void unswizzle8(const uint8_t *src, uint8_t *out, int w, int h)
{
    for (int y = 0; y < h; y++) {
        int block_row = (y & ~0xF) * w, swap = (((y + 2) >> 2) & 1) * 4;
        int column = ((((y & ~3) >> 1) + (y & 1)) & 7) * w * 2, byte_y = (y >> 1) & 1;
        for (int x = 0; x < w; x++)
            out[y * w + x] = src[block_row + (x & ~0xF) * 2 + column + ((x + swap) & 7) * 4 + byte_y + ((x >> 2) & 2)];
    }
}

static uint8_t alpha2(uint8_t a) { return a >= 128 ? 255 : (uint8_t)(a * 2); }

/* ---- the Wii's GX textures (tools/tex.py, docs/FORMAT_BDP.md "The Wii's bundles"): the pixels in tiles, a tile's
 * rows one after the other, the tiles left to right, top to bottom; mip 0 first */
enum { GX_I4 = 0, GX_I8 = 1, GX_IA8 = 3, GX_RGB5A3 = 5, GX_RGBA8 = 6, GX_CMPR = 14 };

static void rgb565(uint16_t c, uint8_t *o)
{
    o[0] = (uint8_t)(((c >> 11) & 31) * 255 / 31);
    o[1] = (uint8_t)(((c >> 5) & 63) * 255 / 63);
    o[2] = (uint8_t)((c & 31) * 255 / 31);
    o[3] = 255;
}

/* one tile's pixels, tw x th, row by row, into tile (RGBA) */
static void gx_tile(int fmt, const uint8_t *p, uint8_t *tile)
{
    switch (fmt) {
    case GX_I4:                                          /* 8 x 8, 4 bits; intensity into alpha too, as GX does */
        for (int k = 0; k < 64; k++) {
            uint8_t v = (uint8_t)((k & 1 ? p[k / 2] & 15 : p[k / 2] >> 4) * 17);
            memset(tile + 4 * k, v, 4);
        }
        break;
    case GX_I8:                                          /* 8 x 4 */
        for (int k = 0; k < 32; k++)
            memset(tile + 4 * k, p[k], 4);
        break;
    case GX_IA8:                                         /* 4 x 4: alpha, intensity */
        for (int k = 0; k < 16; k++) {
            memset(tile + 4 * k, p[2 * k + 1], 3);
            tile[4 * k + 3] = p[2 * k];
        }
        break;
    case GX_RGB5A3:                                      /* 4 x 4, u16: RGB555 opaque, or A3 RGB444 */
        for (int k = 0; k < 16; k++) {
            uint16_t c = rd16be(p + 2 * k);
            uint8_t *o = tile + 4 * k;
            if (c & 0x8000) {
                o[0] = (uint8_t)(((c >> 10) & 31) * 255 / 31); o[1] = (uint8_t)(((c >> 5) & 31) * 255 / 31);
                o[2] = (uint8_t)((c & 31) * 255 / 31); o[3] = 255;
            } else {
                o[0] = (uint8_t)(((c >> 8) & 15) * 17); o[1] = (uint8_t)(((c >> 4) & 15) * 17);
                o[2] = (uint8_t)((c & 15) * 17); o[3] = (uint8_t)(((c >> 12) & 7) * 255 / 7);
            }
        }
        break;
    case GX_RGBA8:                                       /* 4 x 4: the AR pairs, then the GB pairs */
        for (int k = 0; k < 16; k++) {
            uint8_t *o = tile + 4 * k;
            o[0] = p[2 * k + 1]; o[1] = p[32 + 2 * k]; o[2] = p[33 + 2 * k]; o[3] = p[2 * k];
        }
        break;
    case GX_CMPR:                                        /* 8 x 8: four 4 x 4 DXT1 blocks in Z order */
        for (int s = 0; s < 4; s++) {
            const uint8_t *q = p + 8 * s;
            uint16_t c0 = rd16be(q), c1 = rd16be(q + 2);
            uint8_t col[4][4];
            rgb565(c0, col[0]);
            rgb565(c1, col[1]);
            for (int j = 0; j < 3; j++) {
                col[2][j] = (uint8_t)(c0 > c1 ? (2 * col[0][j] + col[1][j]) / 3 : (col[0][j] + col[1][j]) / 2);
                col[3][j] = (uint8_t)(c0 > c1 ? (col[0][j] + 2 * col[1][j]) / 3 : 0);
            }
            col[2][3] = 255;
            col[3][3] = c0 > c1 ? 255 : 0;
            int base = (s >> 1) * 32 + (s & 1) * 4;
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 4; x++)
                    memcpy(tile + 4 * (base + y * 8 + x), col[(q[4 + y] >> (6 - 2 * x)) & 3], 4);
        }
        break;
    }
}

/* a format's tile: its width and height in texels and its bytes; 0 for a format that is none of the Wii's */
static int gx_tile_size(int fmt, int *tw, int *th)
{
    if (fmt != GX_I4 && fmt != GX_I8 && fmt != GX_IA8 && fmt != GX_RGB5A3 && fmt != GX_RGBA8 && fmt != GX_CMPR)
        return 0;
    *tw = fmt == GX_I4 || fmt == GX_I8 || fmt == GX_CMPR ? 8 : 4;
    *th = fmt == GX_I4 || fmt == GX_CMPR ? 8 : 4;
    int bits = fmt == GX_I4 || fmt == GX_CMPR ? 4 : fmt == GX_I8 ? 8 : fmt == GX_RGBA8 ? 32 : 16;
    return *tw * *th * bits / 8;
}

/* p: the texture's pixels (the entry's data2, or as much of it as avail bytes read from the disc) */
static int gx_decode(const BdpEntry *e, const uint8_t *p, size_t avail, uint8_t **rgba, int *w_out, int *h_out)
{
    if (e->size < 0x10)
        return -1;
    int fmt = (int)rd32(e->data + 4), w = rd16(e->data + 8), h = rd16(e->data + 10);   /* swapped by bdp_load */
    int tw = fmt == GX_I4 || fmt == GX_I8 || fmt == GX_CMPR ? 8 : 4, th = fmt == GX_I4 || fmt == GX_CMPR ? 8 : 4;
    int bits = fmt == GX_I4 || fmt == GX_CMPR ? 4 : fmt == GX_I8 ? 8 : fmt == GX_RGBA8 ? 32 : 16;
    if ((fmt != GX_I4 && fmt != GX_I8 && fmt != GX_IA8 && fmt != GX_RGB5A3 && fmt != GX_RGBA8 && fmt != GX_CMPR) ||
        w <= 0 || h <= 0)
        return -1;
    int tiles_x = (w + tw - 1) / tw, tiles_y = (h + th - 1) / th, tile_bytes = tw * th * bits / 8;
    size_t pixels = (size_t)w * h;
    if (pixels > SIZE_MAX / 4 ||
        (size_t)tiles_x * tiles_y > avail / (size_t)tile_bytes)
        return TEX_DECODE_ERROR;
    uint8_t *out = malloc(pixels * 4), tile[8 * 8 * 4];
    if (!out)
        return TEX_DECODE_NOMEM;
    for (int ty = 0; ty < tiles_y; ty++)
        for (int tx = 0; tx < tiles_x; tx++, p += tile_bytes) {
            gx_tile(fmt, p, tile);
            for (int y = 0; y < th && ty * th + y < h; y++) {
                int x0 = tx * tw, n = x0 + tw <= w ? tw : w - x0;
                memcpy(out + 4 * ((size_t)(ty * th + y) * w + x0), tile + 4 * y * tw, (size_t)4 * n);
            }
        }
    *rgba = out;
    *w_out = w;
    *h_out = h;
    return 0;
}

static int ps2_decode(const Bundle *b, const BdpEntry *e, const uint8_t *px, uint8_t **rgba, int *w_out, int *h_out);

/* an 8-bit texture's 256 colours as RGBA (d its header: the palette's name index at 0x20), 0 without one */
static int ps2_palette(const Bundle *b, const uint8_t *d, uint8_t pal[256][4])
{
    uint32_t pal_name = rd32(d + 0x20);
    const char *pn = bdp_name(b, pal_name);
    const BdpEntry *pe = bdp_entry(b, pal_name);
    if (strcmp(pn, "_Alpha8") == 0 || strcmp(pn, "_Intensity8") == 0) {
        int alpha = pn[1] == 'A';
        for (int i = 0; i < 256; i++) {
            pal[i][0] = pal[i][1] = pal[i][2] = alpha ? 255 : (uint8_t)i;
            pal[i][3] = alpha ? (uint8_t)i : 255;
        }
        return 1;
    }
    if (pe && pe->type == ET_PALETTE && pe->size2 >= 1024 && pe->data2) {
        for (int i = 0; i < 256; i++) {          /* CSM1: entries 8-15 and 16-23 of each 32 swapped */
            const uint8_t *c = pe->data2 + 4 * ((i & ~0x18) | ((i & 8) << 1) | ((i & 16) >> 1));
            pal[i][0] = c[0]; pal[i][1] = c[1]; pal[i][2] = c[2]; pal[i][3] = alpha2(c[3]);
        }
        return 1;
    }
    return 0;
}

#ifdef RAMPAGE_XBOX
/* An 8-bit texture as it is stored: its indices in rows and its 256 colours, the pair tex_decode expands to RGBA.
 * The Xbox's GPU takes exactly this (an 8-bit texture with a palette), so the renderer hands it over unexpanded.
 * TEX_DECODE_OK: *index is a malloc'd width x height bytes. TEX_DECODE_ERROR for any texture that is not 8-bit
 * (the caller decodes it as before), and for one with a replacement picture (texpack_xbox.c). */
int tex_indexed(const Bundle *b, const BdpEntry *e, uint8_t **index, uint8_t pal[256][4], int *w_out, int *h_out)
{
    extern int xtp_find(const char *name);
    if (!e || b->wii || e->type != ET_TEXTURE || e->size < 0x24 || !e->size2 || (!e->data2 && (!e->at2 || !b->src)))
        return TEX_DECODE_ERROR;
    const uint8_t *d = e->data;
    int psm = d[4], flags = d[7], w = rd16(d + 8), h = rd16(d + 10);
    size_t pixels = (size_t)w * h;
    if (psm != 0x13 || w <= 0 || h <= 0 || pixels > e->size2 || xtp_find(bdp_name(b, e->name)) >= 0 ||
        !ps2_palette(b, d, pal))
        return TEX_DECODE_ERROR;
    uint8_t *out = malloc(pixels), *fetched = NULL;
    if (!out)
        return TEX_DECODE_NOMEM;
    const uint8_t *px = e->data2;
    if (!px) {                                           /* a lean bundle's: from the disc (as tex_decode) */
        if (flags & 0x40) {
            fetched = malloc(e->size2);
            if (!fetched) {
                free(out);
                return TEX_DECODE_NOMEM;
            }
        }
        /* rows as they are: read straight into the result */
        if (vol_read_range(b->src, &b->src_file, e->at2, fetched ? fetched : out, fetched ? e->size2 : pixels) != 0) {
            free(fetched);
            free(out);
            return TEX_DECODE_ERROR;
        }
        px = fetched;
    }
    if (flags & 0x40)
        unswizzle8(px, out, w, h);
    else if (px)
        memcpy(out, px, pixels);
    free(fetched);
    *index = out;
    *w_out = w;
    *h_out = h;
    return TEX_DECODE_OK;
}
#endif

#ifdef RAMPAGE_XBOX
/* A Wii texture in a format the Xbox's GPU reads, with the mip levels its file has: no expansion to full colour,
 * which the console has no memory for (Dallas's 213 textures are 21 MB that way, and 4 as they are).
 *   CMPR -> TEX_NATIVE_DXT1. A CMPR tile is four DXT1 blocks: the same two colours and sixteen 2-bit choices,
 *           the colours big endian and a row's texels from the high bits down. The same picture, exactly.
 *   RGB5A3 -> TEX_NATIVE_A1R5G5B5 when every texel is of the opaque kind (the same 15 bits: exact), else
 *           TEX_NATIVE_A4R4G4B4 (an opaque texel loses each colour's lowest bit, a see-through one's three bits
 *           of alpha become four).
 *   I8 -> TEX_NATIVE_I8: the first level's bytes in rows (for an 8-bit texture with a grey palette).
 * skip: that many of the largest levels left out, while levels are left and both sides stay at least 8.
 * *data is malloc'd: the levels one after the other (DXT1: 8 bytes a block, rows of blocks; 16-bit: little
 * endian texels in rows). TEX_DECODE_ERROR for any other texture: the caller decodes it as before. */
int tex_gx_native(const Bundle *b, const BdpEntry *e, int skip, int *kind, uint8_t **data, uint32_t *size, int *w_out,
                  int *h_out, int *levels_out)
{
    extern int xtp_find(const char *name);
    if (!e || !b->wii || e->type != ET_TEXTURE || e->size < 0x10 || !e->size2 ||
        (!e->data2 && (!e->at2 || !b->src)) || xtp_find(bdp_name(b, e->name)) >= 0)
        return TEX_DECODE_ERROR;
    int fmt = (int)rd32(e->data + 4), w = rd16(e->data + 8), h = rd16(e->data + 10), levels = rd16(e->data + 12);
    int tw = 0, th = 0, tile_bytes = gx_tile_size(fmt, &tw, &th);
    if ((fmt != GX_CMPR && fmt != GX_RGB5A3 && fmt != GX_I8) || w < 8 || h < 8 || w > 1024 || h > 1024 ||
        (w & (w - 1)) || (h & (h - 1)) || !tile_bytes)
        return TEX_DECODE_ERROR;
    if (levels < 1)
        levels = 1;
    /* the levels the file holds: each half the one before, in whole tiles */
    size_t at[16], bytes[16], have = 0;
    int n = 0;
    for (int l = 0; l < levels && l < 16; l++) {
        int lw = w >> l > 0 ? w >> l : 1, lh = h >> l > 0 ? h >> l : 1;
        size_t need = (size_t)((lw + tw - 1) / tw) * (size_t)((lh + th - 1) / th) * (size_t)tile_bytes;
        if (need > e->size2 - have)
            break;
        at[n] = have;
        bytes[n] = need;
        have += need;
        n++;
        if (lw == 1 && lh == 1)
            break;
    }
    if (!n)
        return TEX_DECODE_ERROR;
    int first = 0;
    while (skip > 0 && first + 1 < n && (w >> (first + 1)) >= 8 && (h >> (first + 1)) >= 8) {
        first++;
        skip--;
    }
    if (fmt == GX_I8)
        n = first + 1;                                   /* (the 8-bit path makes its own smaller levels) */
    size_t src_bytes = at[n - 1] + bytes[n - 1] - at[first];
    const uint8_t *src = e->data2 ? e->data2 + at[first] : NULL;
    uint8_t *fetched = NULL;
    if (!src) {
        fetched = malloc(src_bytes);
        if (!fetched)
            return TEX_DECODE_NOMEM;
        if (vol_read_range(b->src, &b->src_file, e->at2 + (uint32_t)at[first], fetched, src_bytes) != 0) {
            free(fetched);
            return TEX_DECODE_ERROR;
        }
        src = fetched;
    }
    int bw = w >> first, bh = h >> first, k = fmt == GX_CMPR ? TEX_NATIVE_DXT1 : fmt == GX_I8 ? TEX_NATIVE_I8 :
                                                               TEX_NATIVE_A1R5G5B5;
    size_t out_bytes = 0;
    for (int l = first; l < n; l++) {
        int lw = w >> l > 0 ? w >> l : 1, lh = h >> l > 0 ? h >> l : 1;
        out_bytes += fmt == GX_CMPR ? (size_t)(lw >= 4 ? lw / 4 : 1) * (size_t)(lh >= 4 ? lh / 4 : 1) * 8u :
                     fmt == GX_I8 ? (size_t)lw * lh : (size_t)lw * lh * 2u;
    }
    if (fmt == GX_RGB5A3)                                /* any texel of the see-through kind: four bits each */
        for (size_t i = 0; i + 1 < src_bytes; i += 2)
            if (!(src[i] & 0x80)) {
                k = TEX_NATIVE_A4R4G4B4;
                break;
            }
    uint8_t *out = malloc(out_bytes ? out_bytes : 1), *o = out;
    if (!out) {
        free(fetched);
        return TEX_DECODE_NOMEM;
    }
    for (int l = first; l < n; l++) {
        int lw = w >> l > 0 ? w >> l : 1, lh = h >> l > 0 ? h >> l : 1;
        int tiles_x = (lw + tw - 1) / tw, tiles_y = (lh + th - 1) / th;
        const uint8_t *p = src + (at[l] - at[first]);
        if (fmt == GX_CMPR) {
            int blocks_x = lw >= 4 ? lw / 4 : 1, blocks_y = lh >= 4 ? lh / 4 : 1;
            for (int ty = 0; ty < tiles_y; ty++)
                for (int tx = 0; tx < tiles_x; tx++, p += 32)
                    for (int s = 0; s < 4; s++) {
                        int bx = 2 * tx + (s & 1), by = 2 * ty + (s >> 1);
                        if (bx >= blocks_x || by >= blocks_y)
                            continue;
                        const uint8_t *q = p + 8 * s;
                        uint8_t *d = o + 8 * ((size_t)by * blocks_x + bx);
                        d[0] = q[1]; d[1] = q[0]; d[2] = q[3]; d[3] = q[2];
                        for (int r = 0; r < 4; r++) {
                            uint8_t v = q[4 + r];
                            d[4 + r] = (uint8_t)((v & 0x03) << 6 | (v & 0x0C) << 2 | (v & 0x30) >> 2 | (v & 0xC0) >> 6);
                        }
                    }
            o += (size_t)blocks_x * blocks_y * 8u;
        } else if (fmt == GX_I8) {
            for (int ty = 0; ty < tiles_y; ty++)
                for (int tx = 0; tx < tiles_x; tx++, p += 32)
                    for (int y = 0; y < 4 && ty * 4 + y < lh; y++)
                        for (int x = 0; x < 8 && tx * 8 + x < lw; x++)
                            o[(size_t)(ty * 4 + y) * lw + tx * 8 + x] = p[y * 8 + x];
            o += (size_t)lw * lh;
        } else {
            for (int ty = 0; ty < tiles_y; ty++)
                for (int tx = 0; tx < tiles_x; tx++, p += 32)
                    for (int y = 0; y < 4 && ty * 4 + y < lh; y++)
                        for (int x = 0; x < 4 && tx * 4 + x < lw; x++) {
                            uint16_t c = rd16be(p + 2 * (y * 4 + x)), v;
                            if (k == TEX_NATIVE_A1R5G5B5)
                                v = c;
                            else if (c & 0x8000)
                                v = (uint16_t)(0xF000 | ((c >> 11) & 15) << 8 | ((c >> 6) & 15) << 4 | ((c >> 1) & 15));
                            else
                                v = (uint16_t)(((((c >> 12) & 7) * 15 + 3) / 7) << 12 | (c & 0x0FFF));
                            uint8_t *d = o + 2 * ((size_t)(ty * 4 + y) * lw + tx * 4 + x);
                            d[0] = (uint8_t)v;
                            d[1] = (uint8_t)(v >> 8);
                        }
            o += (size_t)lw * lh * 2u;
        }
    }
    free(fetched);
    *kind = k;
    *data = out;
    *size = (uint32_t)out_bytes;
    *w_out = bw;
    *h_out = bh;
    *levels_out = n - first;
    return TEX_DECODE_OK;
}
#endif

int tex_decode(const Bundle *b, const BdpEntry *e, uint8_t **rgba, int *w_out, int *h_out)
{
    if (e && b->wii && e->type == ET_TEXTURE && e->data2)
        return gx_decode(e, e->data2, e->size2, rgba, w_out, h_out);
    if (e && b->wii && e->type == ET_TEXTURE && e->at2 && e->size2 && b->src && e->size >= 0x10) {
        /* a lean bundle's (bdp_open): its pixels are on the disc; the first level's are all that is decoded */
        int tw = 0, th = 0, tile_bytes = gx_tile_size((int)rd32(e->data + 4), &tw, &th);
        int w = rd16(e->data + 8), h = rd16(e->data + 10);
        if (!tile_bytes || w <= 0 || h <= 0)
            return TEX_DECODE_ERROR;
        size_t need = (size_t)((w + tw - 1) / tw) * (size_t)((h + th - 1) / th) * (size_t)tile_bytes;
        if (need > e->size2)
            return TEX_DECODE_ERROR;
        uint8_t *px = malloc(need);
        if (!px)
            return TEX_DECODE_NOMEM;
        int result = vol_read_range(b->src, &b->src_file, e->at2, px, need) == 0
                         ? gx_decode(e, px, need, rgba, w_out, h_out) : TEX_DECODE_ERROR;
        free(px);
        return result;
    }
    if (!e || e->type != ET_TEXTURE || e->size < 0x24)
        return -1;
#ifdef RAMPAGE_XBOX
    {                                                    /* a replacement picture of that name (the Xbox buttons) */
        extern int xtp_find(const char *name);
        extern int xtp_decode(int i, uint8_t **rgba, int *w, int *h);
        int k = xtp_find(bdp_name(b, e->name));
        if (k >= 0) {
            int got = xtp_decode(k, rgba, w_out, h_out);
            if (got == 0) return TEX_DECODE_OK;
            if (got == -2) return TEX_DECODE_NOMEM;
        }                                                /* (unreadable: the bundle's own) */
    }
#endif
    if (!e->data2) {                                     /* a lean bundle's (bdp_open): its pixels are on the disc */
        if (!e->at2 || !e->size2 || !b->src)
            return -1;
        uint8_t *px = malloc(e->size2);
        if (!px)
            return TEX_DECODE_NOMEM;
        int result = vol_read_range(b->src, &b->src_file, e->at2, px, e->size2) == 0
                         ? ps2_decode(b, e, px, rgba, w_out, h_out) : TEX_DECODE_ERROR;
        free(px);
        return result;
    }
    return ps2_decode(b, e, e->data2, rgba, w_out, h_out);
}

static int ps2_decode(const Bundle *b, const BdpEntry *e, const uint8_t *px, uint8_t **rgba, int *w_out, int *h_out)
{
    const uint8_t *d = e->data;
    int psm = d[4], flags = d[7], w = rd16(d + 8), h = rd16(d + 10);
    size_t pixels = (size_t)w * h;
    /* The swizzle offsets and pixel loops use int; byte counts must also fit size_t. */
    if (w <= 0 || h <= 0 || pixels > INT_MAX / 4 || pixels > SIZE_MAX / 4 ||
        (psm == 0x13 ? pixels > e->size2 : psm == 0x00 ? pixels > e->size2 / 4 :
         psm == 0x02 ? pixels > e->size2 / 2 : 1))
        return TEX_DECODE_ERROR;
    uint8_t *out = malloc(pixels * 4);
    if (!out)
        return TEX_DECODE_NOMEM;
    if (psm == 0x13) {
        uint8_t pal[256][4];
        if (!ps2_palette(b, d, pal)) {
            free(out);
            return -1;
        }
        uint8_t *index = malloc(pixels);
        if (!index) {
            free(out);
            return TEX_DECODE_NOMEM;
        }
        if (flags & 0x40)
            unswizzle8(px, index, w, h);
        else
            memcpy(index, px, (size_t)w * h);
        for (int i = 0; i < w * h; i++)
            memcpy(out + 4 * i, pal[index[i]], 4);
        free(index);
    } else if (psm == 0x00) {
        for (int i = 0; i < w * h; i++) {
            memcpy(out + 4 * i, px + 4 * i, 3);
            out[4 * i + 3] = alpha2(px[4 * i + 3]);
        }
    } else if (psm == 0x02) {
        for (int i = 0; i < w * h; i++) {
            uint16_t p = rd16(px + 2 * i);
            out[4 * i] = (uint8_t)((p & 31) << 3);
            out[4 * i + 1] = (uint8_t)(((p >> 5) & 31) << 3);
            out[4 * i + 2] = (uint8_t)(((p >> 10) & 31) << 3);
            out[4 * i + 3] = (p & 0x8000) ? 255 : 0;
        }
    } else {
        free(out);
        return -1;
    }
    *rgba = out;
    *w_out = w;
    *h_out = h;
    return 0;
}
