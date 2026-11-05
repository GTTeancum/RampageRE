/* Rampage Xbox runtime fixes: C99 half-away-from-zero rounding.
 * nxdk's round()/roundf() use ties-to-even; its lround family asserts.
 * Keep these as application objects before libpdclib.lib at link time.
 */
#include <stdint.h>
#include <limits.h>
#include <float.h>
#include <errno.h>
#include <math.h>

static double rounded_double(double value)
{
    union { double f; uint64_t u; } v = { value };
    const uint64_t sign = v.u & UINT64_C(0x8000000000000000);
    const int exponent = (int)((v.u >> 52) & 0x7ffu) - 1023;
    uint64_t mask;
    if (exponent >= 52) return value; /* Integral values, infinities and NaNs. */
    if (exponent < 0) {
        v.u = sign | (exponent == -1 ? UINT64_C(0x3ff0000000000000) : 0);
        return v.f;
    }
    mask = (UINT64_C(1) << (52 - exponent)) - 1;
    if (!(v.u & mask)) return value;
    v.u += UINT64_C(1) << (51 - exponent);
    v.u &= ~mask;
    return v.f;
}

static float rounded_float(float value)
{
    union { float f; uint32_t u; } v = { value };
    const uint32_t sign = v.u & UINT32_C(0x80000000);
    const int exponent = (int)((v.u >> 23) & 0xffu) - 127;
    uint32_t mask;
    if (exponent >= 23) return value;
    if (exponent < 0) {
        v.u = sign | (exponent == -1 ? UINT32_C(0x3f800000) : 0);
        return v.f;
    }
    mask = (UINT32_C(1) << (23 - exponent)) - 1;
    if (!(v.u & mask)) return value;
    v.u += UINT32_C(1) << (22 - exponent);
    v.u &= ~mask;
    return v.f;
}

double round(double value) { return rounded_double(value); }
float roundf(float value) { return rounded_float(value); }

long double roundl(long double value)
{
#if LDBL_MANT_DIG == DBL_MANT_DIG
    return (long double)rounded_double((double)value);
#else
    /* Host-only test portability. Xbox's long double is 64-bit. */
    long double integer = truncl(value);
    long double fraction = value - integer;
    if (fraction >= 0.5L) return integer + 1.0L;
    if (fraction <= -0.5L) return integer - 1.0L;
    return integer;
#endif
}

static long checked_long(long double value)
{
    /* The positive endpoint is exclusive: LONG_MAX may round upward when
       converted to a floating type with fewer integer precision bits. */
    if (!(value >= (long double)LONG_MIN && value < -(long double)LONG_MIN)) {
        errno = EDOM;
        return LONG_MIN;
    }
    return (long)value;
}

static long long checked_long_long(long double value)
{
    if (!(value >= (long double)LLONG_MIN && value < -(long double)LLONG_MIN)) {
        errno = EDOM;
        return LLONG_MIN;
    }
    return (long long)value;
}

long lround(double value) { return checked_long(rounded_double(value)); }
long lroundf(float value) { return checked_long(rounded_float(value)); }
long lroundl(long double value) { return checked_long(roundl(value)); }
long long llround(double value) { return checked_long_long(rounded_double(value)); }
long long llroundf(float value) { return checked_long_long(rounded_float(value)); }
long long llroundl(long double value) { return checked_long_long(roundl(value)); }
