/* vol.c - RAMPAGE.VOL, the game's data archive (tools/pvol.py): 352 named files stored one after the other. Read from
 * the file itself or in place from the disc image (iso.c): every offset is then from Vol.base. */
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#include "data.h"
#include "iso.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

int vol_open(Vol *v, const char *path)
{
    uint8_t head[16], *index = NULL;
    int result = VOL_UNREADABLE;
    memset(v, 0, sizeof *v);
    v->f = fopen(path, "rb");
    if (!v->f || fread(head, 1, 16, v->f) != 16)
        goto failed;
    if (memcmp(head, "PVOL", 4) != 0) {                 /* a disc image: its RAMPAGE.VOL */
        uint32_t size;
        if (!iso_is(v->f))
            goto failed;
        if (!iso_find(v->f, "RAMPAGE.VOL", &v->base, &size) || size < 16 || _fseeki64(v->f, v->base, SEEK_SET) != 0 ||
            fread(head, 1, 16, v->f) != 16 || memcmp(head, "PVOL", 4) != 0) {
            result = VOL_NOT_IN_IMAGE;
            goto failed;
        }
    }
    uint32_t count = rd32(head + 8), names_size = rd32(head + 12);
    index = malloc(12u * count);
    v->names = malloc(names_size + 1);
    v->files = calloc(count, sizeof *v->files);
    if (!index || !v->names || !v->files || fread(index, 12, count, v->f) != count ||
        fread(v->names, 1, names_size, v->f) != names_size)
        goto failed;
    v->names[names_size] = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t at = rd32((uint8_t *)v->names + 4 + 4 * i);
        v->files[i].offset = rd32(index + 12 * i);
        v->files[i].size = rd32(index + 12 * i + 4);
        v->files[i].name = at < names_size ? v->names + at : "";
    }
    v->count = count;
    free(index);
    return 0;
failed:
    free(index);
    free(v->names);
    free(v->files);
    if (v->f)
        fclose(v->f);
    memset(v, 0, sizeof *v);
    return result;
}

static const VolFile *find_in(const VolFile *files, uint32_t n, const char *name)
{
    for (uint32_t i = 0; i < n; i++)
        if (_stricmp(files[i].name, name) == 0)
            return &files[i];
    return NULL;
}

const VolFile *vol_find(const Vol *v, const char *name)
{
    const VolFile *f = find_in(v->files, v->count, name);
    if (f || !v->nwii)
        return f;
    if ((f = find_in(v->wii, v->nwii, name)) != NULL)
        return f;
    size_t n = strlen(name);                             /* the Wii's bundles: .bdg for .bdp, .cmg for .cmp */
    if (n < 4 || n >= 256 || (_stricmp(name + n - 4, ".bdp") != 0 && _stricmp(name + n - 4, ".cmp") != 0))
        return NULL;
    char wii[256];
    memcpy(wii, name, n + 1);
    wii[n - 1] = 'g';
    return find_in(v->wii, v->nwii, wii);
}

int vol_read_range(const Vol *v, const VolFile *file, uint32_t offset, void *out, size_t size)
{
    if (!v || !file || offset > file->size || size > file->size - offset || (!out && size))
        return -1;
    if (!size)
        return 0;
    FILE *h = file->path ? fopen(file->path, "rb") : v->f;
    int64_t base = file->path ? 0 : v->base;
    if (base < 0 || base > INT64_MAX - (int64_t)file->offset - offset) {
        if (file->path && h) fclose(h);
        return -1;
    }
    int ok = h && _fseeki64(h, base + file->offset + offset, SEEK_SET) == 0 && fread(out, 1, size, h) == size;
    if (file->path && h)
        fclose(h);
    return ok ? 0 : -1;
}

uint8_t *vol_read(const Vol *v, const char *name, uint32_t *size)
{
    const VolFile *f = vol_find(v, name);
    if (!f)
        return NULL;
    uint8_t *buf = malloc(f->size ? f->size : 1);
    FILE *h = f->path ? fopen(f->path, "rb") : v->f;
    int ok = buf && h && _fseeki64(h, (f->path ? 0 : v->base) + f->offset, SEEK_SET) == 0 &&
             fread(buf, 1, f->size, h) == f->size;
    if (f->path && h)
        fclose(h);
    if (!ok) {
        free(buf);
        return NULL;
    }
    if (size)
        *size = f->size;
    return buf;
}

uint8_t *vol_read_wii(const Vol *v, const char *name, uint32_t *size)
{
    const VolFile *f = find_in(v->wii, v->nwii, name);
    FILE *h = f ? fopen(f->path, "rb") : NULL;
    uint8_t *buf = h ? malloc(f->size ? f->size : 1) : NULL;
    int ok = buf && _fseeki64(h, f->offset, SEEK_SET) == 0 && fread(buf, 1, f->size, h) == f->size;
    if (h)
        fclose(h);
    if (!ok) {
        free(buf);
        return NULL;
    }
    if (size)
        *size = f->size;
    return buf;
}

/* ---- the Wii folder; sep '/': dir/name is the file, 0: name is in dir (a bundle APK) at offset */
static void add_file(Vol *v, uint32_t *cap, const char *dir, const char *name, uint32_t size, uint32_t offset, char sep)
{
    if (v->nwii == *cap) {
        if (*cap > UINT32_MAX / 2)
            return;
        uint32_t next = *cap ? *cap * 2 : 256;
        if ((uint64_t)next * sizeof *v->wii > SIZE_MAX)
            return;
        VolFile *grown = realloc(v->wii, sizeof *v->wii * next);
        if (!grown)
            return;
        v->wii = grown;
        *cap = next;
    }
    size_t dn = strlen(dir), nn = strlen(name);
    char *path = malloc(dn + 1 + nn + 1);
    if (!path)
        return;
    memcpy(path, dir, dn);
    path[dn] = sep;
    memcpy(path + dn + 1, name, nn + 1);
    v->wii[v->nwii++] = (VolFile){ path + dn + 1, offset, size, path };
}

typedef struct { Vol *v; uint32_t *cap; const char *apk; } Zipped;
static void add_zipped(void *ctx, const char *name, int64_t at, uint32_t size)
{
    Zipped *z = ctx;
    add_file(z->v, z->cap, z->apk, name, size, (uint32_t)at, 0);
}

static void add_dir(Vol *v, uint32_t *cap, const char *dir)
{
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    char pat[1024];
    snprintf(pat, sizeof pat, "%s/*", dir);
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            add_file(v, cap, dir, fd.cFileName, fd.nFileSizeLow, 0, '/');
    while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    struct dirent *de;
    if (!d)
        return;
    while ((de = readdir(d)) != NULL) {
        char path[1024];
        struct stat st;
        snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
            add_file(v, cap, dir, de->d_name, (uint32_t)st.st_size, 0, '/');
    }
    closedir(d);
#endif
}

uint32_t vol_add_wii(Vol *v, const char *dir)
{
    static const char *const UNDER[] = { "", "/DATA/files", "/files" };
    static const char *const SUB[] = { "Game", "Shell", "Audio" };
    char root[1024], probe[1100];
    FILE *apk = fopen(dir, "rb");                        /* a bundle APK: its assets/wii/Game/ ... (a folder: none) */
    if (apk) {
        uint32_t cap = 0;
        Zipped z = { v, &cap, dir };
        for (size_t s = 0; s < sizeof SUB / sizeof *SUB; s++) {
            snprintf(probe, sizeof probe, "wii/%s/", SUB[s]);
            if (!zip_each(apk, probe, add_zipped, &z) && !s)
                break;                                   /* no Game: not the Wii's */
        }
        fclose(apk);
        if (v->nwii)
            return v->nwii;
    }
    for (size_t k = 0; k < sizeof UNDER / sizeof *UNDER; k++) {
        snprintf(root, sizeof root, "%s%s", dir, UNDER[k]);
        snprintf(probe, sizeof probe, "%s/Game", root);
        if (!plat_exists(probe))
            continue;
        uint32_t cap = 0;
        for (size_t s = 0; s < sizeof SUB / sizeof *SUB; s++) {
            snprintf(probe, sizeof probe, "%s/%s", root, SUB[s]);
            add_dir(v, &cap, probe);
        }
        return v->nwii;
    }
    return 0;
}
