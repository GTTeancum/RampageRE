#ifndef AUDIT_ALLOC_PROBE_H
#define AUDIT_ALLOC_PROBE_H
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
void *audit_malloc(size_t n, const char *file, int line);
void *audit_calloc(size_t n, size_t s, const char *file, int line);
void *audit_realloc(void *p, size_t n, const char *file, int line);
void audit_free(void *p);
size_t audit_fread(void *ptr, size_t size, size_t count, FILE *f);
void audit_report(const char *label);
size_t audit_bytes(void);
size_t audit_live(void);
void audit_fail_at(size_t call);
size_t audit_calls(void);
#ifndef AUDIT_ALLOC_IMPL
#define malloc(n) audit_malloc(n,__FILE__,__LINE__)
#define calloc(n,s) audit_calloc(n,s,__FILE__,__LINE__)
#define realloc(p,n) audit_realloc(p,n,__FILE__,__LINE__)
#define free(p) audit_free(p)
#define fread(p,s,n,f) audit_fread(p,s,n,f)
#endif
#endif
