#ifndef MOVES_LIFETIMES_PROBE_H
#define MOVES_LIFETIMES_PROBE_H
#include <stddef.h>
void *audit_calloc(int domain, size_t n, size_t size);
void audit_free(int domain, void *p);
#endif
