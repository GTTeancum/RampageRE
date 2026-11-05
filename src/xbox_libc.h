#ifndef RAMPAGE_XBOX_LIBC_H
#define RAMPAGE_XBOX_LIBC_H
#include <stddef.h>
#include <stdarg.h>
double plat_xbox_strtod(const char *text, char **end);
float plat_xbox_strtof(const char *text, char **end);
double plat_xbox_atof(const char *text);
int plat_xbox_vsnprintf(char *out, size_t size, const char *fmt, va_list ap);
int plat_xbox_snprintf(char *out, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
#endif
