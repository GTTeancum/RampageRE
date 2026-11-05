/* nxdk OS services. D: is nxdk's launch directory (HDD or optical disc).
 * Persistent files live on E: so a read-only XISO can save profiles/settings.
 * All file offsets remain 64-bit through PDCLib's fpos_t, including disc images. */
#ifdef RAMPAGE_XBOX
#include "plat.h"
#undef fopen
#undef FindFirstFileA
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <hal/debug.h>
#include <nxdk/mount.h>
#include <xboxkrnl/xboxkrnl.h>

static int home_ready;
static int save_ready;
static const char *save_root = "E:\\UDATA\\RampageRE";

int plat_xbox_strnicmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        int x = tolower((unsigned char)a[i]), y = tolower((unsigned char)b[i]);
        if (x != y || !x || !y) return x - y;
    }
    return 0;
}
int plat_xbox_stricmp(const char *a, const char *b)
{
    size_t an = strlen(a), bn = strlen(b);
    return plat_xbox_strnicmp(a, b, (an < bn ? an : bn) + 1);
}

/* Keep all paths absolute; nxdk has no process working directory. */
int plat_xbox_path(const char *path, char *out, size_t cap, int writable)
{
    if (!path || !*path || cap < 4) { errno = EINVAL; return 0; }
    const char *root = writable ? save_root : "D:";
    int absolute = path[0] && path[1] == ':';
    int n = absolute ? snprintf(out, cap, "%s", path) : snprintf(out, cap, "%s\\%s", root, path);
    if (n < 0 || (size_t)n >= cap || n >= MAX_PATH) { errno = ENAMETOOLONG; return 0; }
    for (char *p = out; *p; ++p) if (*p == '/') *p = '\\';
    /* Collapse . and duplicate separators; refuse parent traversal. */
    char normalized[MAX_PATH];
    size_t o = 3;
    normalized[0] = out[0]; normalized[1] = ':'; normalized[2] = '\\';
    const char *p = out + 2;
    while (*p) {
        while (*p == '\\') ++p;
        const char *e = p; while (*e && *e != '\\') ++e;
        size_t len = (size_t)(e - p);
        if (!len) break;
        if (len == 2 && p[0] == '.' && p[1] == '.') { errno = EINVAL; return 0; }
        if (!(len == 1 && p[0] == '.')) {
            if (o + (o > 3 ? 1 : 0) + len >= sizeof normalized) { errno = ENAMETOOLONG; return 0; }
            if (o > 3) normalized[o++] = '\\';
            memcpy(normalized + o, p, len); o += len;
        }
        p = e;
    }
    normalized[o] = 0;
    if (o + 1 > cap) { errno = ENAMETOOLONG; return 0; }
    memcpy(out, normalized, o + 1);
    return 1;
}
static int persistent(const char *path)
{
    while (path[0] == '.' && (path[1] == '/' || path[1] == '\\')) path += 2;
    return !plat_xbox_stricmp(path, "rampage.cfg") || !plat_xbox_strnicmp(path, "rampage_profile", 15) ||
           !plat_xbox_stricmp(path, "disc.txt") || !plat_xbox_stricmp(path, "rampage.log");
}
FILE *plat_fopen(const char *path, const char *mode)
{
    char resolved[MAX_PATH];
    if (!mode || !*mode) { errno = EINVAL; return NULL; }
    int writing = mode[0] != 'r' || strchr(mode, '+') != NULL;
    int save = writing || persistent(path ? path : "");
    if (!plat_xbox_path(path, resolved, sizeof resolved, save)) return NULL;
    if (writing && !save_ready && !(path[0] && path[1] == ':')) { errno = EACCES; return NULL; }
    FILE *f = fopen(resolved, mode);
    /* A bundled default rampage.cfg is useful, but must never overwrite a saved one. */
    if (!f && !writing && save && plat_xbox_path(path, resolved, sizeof resolved, 0)) f = fopen(resolved, mode);
    return f;
}
int plat_exists(const char *path)
{
    char resolved[MAX_PATH];
    if (persistent(path ? path : "") && plat_xbox_path(path, resolved, sizeof resolved, 1) &&
        GetFileAttributesA(resolved) != INVALID_FILE_ATTRIBUTES) return 1;
    return plat_xbox_path(path, resolved, sizeof resolved, 0) && GetFileAttributesA(resolved) != INVALID_FILE_ATTRIBUTES;
}
HANDLE plat_xbox_find_first(const char *path, WIN32_FIND_DATAA *data)
{
    char resolved[MAX_PATH];
    if (!plat_xbox_path(path, resolved, sizeof resolved, 0)) return INVALID_HANDLE_VALUE;
    return FindFirstFileA(resolved, data);
}
long long plat_free_kb(const char *path)
{
    (void)path;
    ULARGE_INTEGER bytes;
    return save_ready && GetDiskFreeSpaceExA("E:\\", &bytes, NULL, NULL) ? (long long)(bytes.QuadPart / 1024) : -1;
}
int64_t plat_xbox_ftelli64(FILE *f)
{
    fpos_t pos;
    if (fgetpos(f, &pos) != 0 || pos.offset > INT64_MAX) return -1;
    return (int64_t)pos.offset;
}
int plat_xbox_fseeki64(FILE *f, int64_t off, int whence)
{
    fpos_t pos;
    int64_t base = 0;
    if (whence == SEEK_END) {
        if (fseek(f, 0, SEEK_END) != 0) return -1;
    } else if (whence != SEEK_SET && whence != SEEK_CUR) { errno = EINVAL; return -1; }
    if (fgetpos(f, &pos) != 0 || pos.offset > INT64_MAX) return -1;
    if (whence != SEEK_SET) base = (int64_t)pos.offset;
    if ((off > 0 && base > INT64_MAX - off) || (off < 0 && off < -base)) { errno = EINVAL; return -1; }
    pos.offset = (uint64_t)(base + off);
    if (fsetpos(f, &pos) != 0) return -1;
    clearerr(f); /* PDCLib fsetpos currently does not clear EOF. */
    return 0;
}
void plat_message(const char *text, int warn)
{
    (void)warn;
    fprintf(stderr, "%s\n", text);
    if (!plat_quiet) { debugPrint("%s\n", text); plat_told = 1; }
}
void plat_xbox_memory_tick(void)
{
    static DWORD sampled_at, reported_at;
    static unsigned long peak_kb;
    static int sampled, reported;
    DWORD now = GetTickCount();
    if (sampled && (DWORD)(now - sampled_at) < 1000) return;
    sampled_at = now; sampled = 1;
    MM_STATISTICS st;
    memset(&st, 0, sizeof st);
    st.Length = sizeof st;
    if (!NT_SUCCESS(MmQueryStatistics(&st)) || st.AvailablePages > st.TotalPhysicalPages) return;
    unsigned long total_kb = st.TotalPhysicalPages * 4ul;
    unsigned long free_kb = st.AvailablePages * 4ul;
    unsigned long used_kb = total_kb - free_kb;
    if (used_kb > peak_kb) peak_kb = used_kb;
    if (reported && (DWORD)(now - reported_at) < 5000) return;
    reported_at = now; reported = 1;
    printf("xbox physical_memory total_kb=%lu used_kb=%lu available_kb=%lu peak_sampled_kb=%lu\n",
           total_kb, used_kb, free_kb, peak_kb);
    fflush(stdout);
}
void plat_xbox_home(void)
{
    if (home_ready) return;
    home_ready = 1;
    if (!nxIsDriveMounted('E')) nxMountDrive('E', "\\Device\\Harddisk0\\Partition1");
    if (nxIsDriveMounted('E')) {
        CreateDirectoryA("E:\\UDATA", NULL);
        CreateDirectoryA(save_root, NULL);
        save_ready = GetFileAttributesA(save_root) != INVALID_FILE_ATTRIBUTES;
    }
    /* Leave stdout/stderr on nxdk's debug stream so emulator logs remain visible.
     * The explicit logs/profiles use plat_fopen and the persistent directory. */
    plat_xbox_memory_tick();
    printf("RampageRE Xbox: data D:\\, saves %s (%s)\n", save_root, save_ready ? "ready" : "unavailable");
}
#endif
