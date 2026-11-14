/* xbox_math.h - included ahead of every game source of the Xbox build (platform/xbox/CMakeLists.txt, -include).
 * nxdk's C library makes fminf and fmaxf calls into fmin.c / fmax.c, which classify both arguments through two
 * more calls (__fpclassifyf, __signbitf): 3 % of a frame's CPU time in the four-monster profile, for a compare.
 * Here they are the compare, with the library's result for a NaN argument (the other argument). */
#ifndef RAMPAGE_XBOX_MATH_H
#define RAMPAGE_XBOX_MATH_H
#include <math.h>
static inline float xbox_fminf(float a, float b) { return (a < b || b != b) ? a : b; }
static inline float xbox_fmaxf(float a, float b) { return (a > b || b != b) ? a : b; }
#undef fminf
#undef fmaxf
#define fminf xbox_fminf
#define fmaxf xbox_fmaxf
#endif
