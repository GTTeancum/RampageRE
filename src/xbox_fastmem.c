/* Fast replacements for the C library's hottest routines on Xbox.
 *
 * An nxdk whose PDCLib was built without optimisation ships byte-at-a-time
 * memcpy/memset/strlen with every temporary on the stack and a software
 * sqrt. On the real console they were a fifth of all frame time (vertex
 * staging, the mixer, name lookups). Objects win over library members at
 * link time, so defining them here replaces the library's without touching
 * the installed nxdk.
 *
 * Compiled with -fno-builtin (platform/xbox/CMakeLists.txt) so the compiler
 * cannot turn these bodies back into calls to themselves. The x86 ABI
 * guarantees the direction flag is clear on entry. */
#ifdef RAMPAGE_XBOX
#include <stddef.h>
/* (the functions themselves: not the macros of xbox_builtins.h) */
#undef memcpy
#undef memmove
#undef memset
#undef memcmp
#undef strlen
#undef sqrtf
#undef sqrt
#undef fabsf
#undef fabs

void *memcpy(void *dst, const void *src, size_t n)
{
    void *ret = dst;
    size_t words = n >> 2, tail = n & 3;
    __asm__ volatile("rep movsl" : "+D"(dst), "+S"(src), "+c"(words) : : "memory");
    __asm__ volatile("rep movsb" : "+D"(dst), "+S"(src), "+c"(tail) : : "memory");
    return ret;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d <= s || d >= s + n)
        return memcpy(dst, src, n);
    d += n;                                  /* overlapping with dst above src: copy backwards */
    s += n;
    while (n >= 4) {
        d -= 4; s -= 4; n -= 4;
        unsigned v;
        __builtin_memcpy(&v, s, 4);
        __builtin_memcpy(d, &v, 4);
    }
    while (n--) *--d = *--s;
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    void *ret = dst;
    unsigned fill = (unsigned char)c * 0x01010101u;
    size_t words = n >> 2, tail = n & 3;
    __asm__ volatile("rep stosl" : "+D"(dst), "+c"(words) : "a"(fill) : "memory");
    __asm__ volatile("rep stosb" : "+D"(dst), "+c"(tail) : "a"(fill) : "memory");
    return ret;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = a, *q = b;
    for (; n >= 4; n -= 4, p += 4, q += 4) {             /* whole words while they are equal */
        unsigned x, y;
        __builtin_memcpy(&x, p, 4);
        __builtin_memcpy(&y, q, 4);
        if (x != y) break;
    }
    for (; n; n--, p++, q++)
        if (*p != *q) return *p < *q ? -1 : 1;
    return 0;
}

/* The library's square roots are unoptimised x87 thunks, and nxdk compiles with -fno-builtin, so every sqrtf in the
 * game was a call into one. All three are defined: the library keeps them in one object. */
float sqrtf(float x) { return __builtin_sqrtf(x); }
double sqrt(double x) { return __builtin_sqrt(x); }
long double sqrtl(long double x) { return __builtin_sqrtl(x); }
#endif
