/* nxdk OS services. D: is nxdk's launch directory (HDD or optical disc).
 * Persistent files live on E: so a read-only XISO can save profiles/settings.
 * All file offsets remain 64-bit through PDCLib's fpos_t, including disc images. */
#ifdef RAMPAGE_XBOX
#include "plat.h"
#undef fopen
#undef printf
#undef fprintf
#undef vfprintf
#undef FindFirstFileA
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <hal/debug.h>
#include <nxdk/mount.h>
#include <xboxkrnl/xboxkrnl.h>

static int home_ready;
static int save_ready;
static int stdout_ready, stderr_ready;
static const char *save_root = "E:\\UDATA\\RampageRE";

/* nxdk's default standard streams have no backing HANDLE. Mirror diagnostics
 * to the kernel debug channel and, when mounted, separate persistent logs. */
static int diagnostic(FILE *f, const char *fmt, va_list ap)
{
    char small[2048], *text = small;
    va_list copy;
    va_copy(copy, ap);
    int n = plat_xbox_vsnprintf(small, sizeof small, fmt, copy);
    va_end(copy);
    if (n < 0) return n;
    if ((size_t)n >= sizeof small) {
        text = malloc((size_t)n + 1);
        if (!text) return -1;
        va_copy(copy, ap);
        plat_xbox_vsnprintf(text, (size_t)n + 1, fmt, copy);
        va_end(copy);
    }
    /* The kernel debug channel is slow with a debugger attached (a network round trip a line): errors always,
     * ordinary log lines only in test builds. */
#ifdef RAMPAGE_XBOX_TEST_ARGS
    if (f == stdout || f == stderr) DbgPrint("%s", text);
#else
    if (f == stderr) DbgPrint("%s", text);
#endif
    int write = (f != stdout && f != stderr) || (f == stdout && stdout_ready) || (f == stderr && stderr_ready);
    int ok = !write || fwrite(text, 1, (size_t)n, f) == (size_t)n;
    if (text != small) free(text);
    return ok ? n : -1;
}
int plat_xbox_vfprintf(FILE *f, const char *fmt, va_list ap) { return diagnostic(f, fmt, ap); }

int plat_xbox_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = diagnostic(stdout, fmt, ap);
    va_end(ap);
    return n;
}
int plat_xbox_fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = diagnostic(f, fmt, ap);
    va_end(ap);
    return n;
}

/* One pass, no library calls: the game compares names (properties, bundle entries, sounds) all the time, and
 * two strlen calls plus a tolower call a character were several percent of a frame. "C" locale lower case. */
static inline int lower_c(unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int plat_xbox_strnicmp(const char *a, const char *b, size_t n)
{
    const unsigned char *p = (const unsigned char *)a, *q = (const unsigned char *)b;
    for (; n; --n, ++p, ++q) {
        if (*p != *q) {
            int x = lower_c(*p), y = lower_c(*q);
            if (x != y) return x - y;
        }
        if (!*p) return 0;
    }
    return 0;
}
int plat_xbox_stricmp(const char *a, const char *b)
{
    const unsigned char *p = (const unsigned char *)a, *q = (const unsigned char *)b;
    for (;; ++p, ++q) {
        if (*p != *q) {
            int x = lower_c(*p), y = lower_c(*q);
            if (x != y) return x - y;
        }
        if (!*p) return 0;
    }
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
    plat_xbox_fprintf(stderr, "%s\n", text);
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
    plat_xbox_printf("xbox physical_memory total_kb=%lu used_kb=%lu available_kb=%lu peak_sampled_kb=%lu\n",
           total_kb, used_kb, free_kb, peak_kb);
#ifdef RAMPAGE_XBOX_TEST_ARGS
    /* the kernel's own split; what is left over is GPU (contiguous) memory and page tables */
    {
        unsigned long heap_kb = st.VirtualMemoryBytesCommitted / 1024ul;
        unsigned long cache_kb = st.CachePagesCommitted * 4ul, pool_kb = st.PoolPagesCommitted * 4ul;
        unsigned long stack_kb = st.StackPagesCommitted * 4ul, image_kb = st.ImagePagesCommitted * 4ul;
        unsigned long named = heap_kb + cache_kb + pool_kb + stack_kb + image_kb;
        plat_xbox_printf("xbox memory_split heap_kb=%lu file_cache_kb=%lu pool_kb=%lu stack_kb=%lu image_kb=%lu gpu_and_other_kb=%lu\n",
               heap_kb, cache_kb, pool_kb, stack_kb, image_kb, used_kb > named ? used_kb - named : 0ul);
    }
    {
        /* (to the disc every 30 s, not with every report: the write stalled the frame it fell in for a tenth of
         * a second, and every fifth second of a test run had such a frame) */
        static DWORD flushed_at;
        if ((DWORD)(now - flushed_at) >= 30000) {
            flushed_at = now;
            printf("xbox log written to the disc\n");
            fflush(stdout);
        }
    }
#endif
}
/* The console's audio processor (the MCPX APU, registers at 0xFE800000) belongs to whatever ran before: a dashboard
 * or game that played sound through it. It mixes into pages of RAM it was told about, and this program never uses
 * it (its sound goes to the AC97 interface directly), so nothing here would ever tell it to stop. A harness run
 * launched over a running dashboard came up with five pages of its own code overwritten by six-channel sound
 * frames and died on an illegal instruction. The voice processor's frame counter is switched off and both DSPs are
 * put in reset before anything is allocated: the state a cold start leaves it in. */
static void quiet_apu(void)
{
    volatile uint32_t *apu = (volatile uint32_t *)0xFE800000;
    apu[0x2000 / 4] &= ~0x18u;                       /* NV_PAPU_SECTL: XCNTMODE off */
    apu[0x3FFFC / 4] = 0;                            /* NV_PAPU_GPRST: the global processor and its DSP in reset */
    apu[0x5FFFC / 4] &= ~2u;                         /* NV_PAPU_EPRST: the encode processor's DSP in reset */
}

void plat_xbox_home(void)
{
    if (home_ready) return;
    home_ready = 1;
    quiet_apu();
    if (!nxIsDriveMounted('E')) nxMountDrive('E', "\\Device\\Harddisk0\\Partition1");
    if (nxIsDriveMounted('E')) {
        CreateDirectoryA("E:\\UDATA", NULL);
        CreateDirectoryA(save_root, NULL);
        save_ready = GetFileAttributesA(save_root) != INVALID_FILE_ATTRIBUTES;
    }
    if (save_ready) {
        char path[MAX_PATH];
        if (plat_xbox_path("rampage.log", path, sizeof path, 1))
            stdout_ready = freopen(path, "w", stdout) != NULL;
        if (plat_xbox_path("rampage.err", path, sizeof path, 1))
            stderr_ready = freopen(path, "w", stderr) != NULL;
        /* The log is buffered whole: a line written to the hard disk in the middle of play is a hitch.
         * It reaches the disk when the buffer fills, at exit, and every 5 s in test builds (memory tick). */
        static char log_buffer[32768];
        if (stdout_ready) setvbuf(stdout, log_buffer, _IOFBF, sizeof log_buffer);
        if (stderr_ready) setvbuf(stderr, NULL, _IONBF, 0);
    }
    plat_xbox_memory_tick();
    plat_xbox_printf("RampageRE Xbox: data D:\\, saves %s (%s)\n", save_root, save_ready ? "ready" : "unavailable");
}
#endif
