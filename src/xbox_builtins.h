/* Force-included in every Xbox source (platform/xbox/build_windows.sh, -include).
 *
 * nxdk compiles with -fno-builtin, so a 12-byte memcpy, a memset of a small struct or a sqrtf is a real call into
 * the C library, and the game makes millions of them a second (the skinning loop stored each posed vertex with two
 * memcpy calls). Naming the compiler's builtins brings back what every other platform's build gets: small fixed
 * copies become a few moves, sqrtf one instruction. A size the compiler cannot see still calls the library's
 * function (src/xbox_fastmem.c). The headers come first, so their declarations are not touched. */
#ifndef RAMPAGE_XBOX_BUILTINS_H
#define RAMPAGE_XBOX_BUILTINS_H
#if !defined(__ASSEMBLER__) && !defined(XBOX_NO_BUILTIN_MACROS)
#include <math.h>
#include <string.h>
#define memcpy(d, s, n) __builtin_memcpy((d), (s), (n))
#define memmove(d, s, n) __builtin_memmove((d), (s), (n))
#define memset(d, c, n) __builtin_memset((d), (c), (n))
#define memcmp(a, b, n) __builtin_memcmp((a), (b), (n))
#define strlen(s) __builtin_strlen(s)
#define sqrtf(x) __builtin_sqrtf(x)
#define sqrt(x) __builtin_sqrt(x)
#define fabsf(x) __builtin_fabsf(x)
#define fabs(x) __builtin_fabs(x)
#ifdef RAMPAGE_XBOX
/* The game asks the environment for its test switches (RAMPAGE_TRACE and a hundred others) inside its per-frame
 * code. The console has no environment: the answer is always "not set", and asking was a library call and a string
 * search each time. In the game's own sources only (gl4es reads its settings its own way). */
#include <stdlib.h>
#define getenv(name) ((char *)0)
#endif
#endif
#endif
