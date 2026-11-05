/* nxdk lacks floating strtod/strtof and printf conversions. The game's text
 * assets use C-locale decimal/scientific numbers; support hex, inf and nan too.
 * Formatting uses stb_sprintf already provided by nxdk-gles11 (public domain).
 * Decimal conversion retains 18 significant digits; it is not a replacement
 * for a correctly-rounded arbitrary-precision scientific parsing library. */
#include "xbox_libc.h"
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>

int stbsp_vsnprintf(char *, int, const char *, va_list);
int plat_xbox_vsnprintf(char *out, size_t size, const char *fmt, va_list ap)
{
    return stbsp_vsnprintf(out, size > INT_MAX ? INT_MAX : (int)size, fmt, ap);
}
int plat_xbox_snprintf(char *out, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = plat_xbox_vsnprintf(out, size, fmt, ap);
    va_end(ap);
    return n;
}
static int word(const char *s, const char *w)
{
    while (*w) if (tolower((unsigned char)*s++) != *w++) return 0;
    return 1;
}
static int digit(int c, int base)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (base == 16 && c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (base == 16 && c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
double plat_xbox_strtod(const char *text, char **end)
{
    const char *p = text;
    if (end) *end = (char *)text;
    if (!text) { errno = EINVAL; return 0; }
    while (isspace((unsigned char)*p)) ++p;
    int negative = *p == '-';
    if (*p == '+' || *p == '-') ++p;
    if (word(p, "inf")) {
        p += 3; if (word(p, "inity")) p += 5;
        if (end) *end = (char *)p;
        return negative ? -HUGE_VAL : HUGE_VAL;
    }
    if (word(p, "nan")) {
        p += 3;
        if (*p == '(') {
            const char *q = p + 1;
            while (isalnum((unsigned char)*q) || *q == '_') ++q;
            if (*q == ')') p = q + 1;
        }
        if (end) *end = (char *)p;
        return negative ? -NAN : NAN;
    }
    int base = 10, scale = 1, limit = 18;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
        (digit((unsigned char)p[2], 16) >= 0 || (p[2] == '.' && digit((unsigned char)p[3], 16) >= 0))) {
        base = 16; scale = 4; limit = 13; p += 2;
    }
    uint64_t mantissa = 0;
    int fraction = 0, any = 0, significant = 0, exponent = 0;
    for (;;) {
        int d = digit((unsigned char)*p, base);
        if (d >= 0) {
            any = 1;
            if (!mantissa && d == 0) {
                if (fraction && exponent > -1000000) exponent -= scale;
            } else if (significant < limit) {
                mantissa = mantissa * (unsigned)base + (unsigned)d;
                ++significant;
                if (fraction && exponent > -1000000) exponent -= scale;
            } else if (!fraction && exponent < 1000000) exponent += scale;
            ++p;
        } else if (*p == '.' && !fraction) { fraction = 1; ++p; }
        else break;
    }
    if (!any) return 0;
    if (*p == (base == 10 ? 'e' : 'p') || *p == (base == 10 ? 'E' : 'P')) {
        const char *q = p + 1;
        int minus = *q == '-';
        if (*q == '+' || *q == '-') ++q;
        if (isdigit((unsigned char)*q)) {
            int e = 0;
            do {
                if (e < 1000000) e = e * 10 + (*q - '0');
                ++q;
            } while (isdigit((unsigned char)*q));
            exponent += minus ? -e : e;
            p = q;
        }
    }
    if (end) *end = (char *)p;
    double value = (double)mantissa;
    if (mantissa) {
        if (base == 16) value = ldexp(value, exponent);
        else if (exponent > 308) value = HUGE_VAL;
        else if (exponent < -650) value = 0;
        else {
            /* Scale the mantissa first to retain subnormal results; computing
             * pow(10,-324) before multiplying would discard them. */
            while (exponent < -308) { value *= 1e-308; exponent += 308; }
            value *= pow(10.0, (double)exponent);
        }
        if (!isfinite(value) || value < DBL_MIN) errno = ERANGE;
    }
    return negative ? -value : value;
}
float plat_xbox_strtof(const char *text, char **end)
{
    double value = plat_xbox_strtod(text, end);
    float result = (float)value;
    if (isfinite(value) && value != 0 && (!isfinite(result) || fabsf(result) < FLT_MIN)) errno = ERANGE;
    return result;
}
double plat_xbox_atof(const char *text) { return plat_xbox_strtod(text, NULL); }
