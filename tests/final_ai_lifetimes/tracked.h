#ifndef FINAL_AI_TRACKED_H
#define FINAL_AI_TRACKED_H
#include <stddef.h>
void *dir_malloc(size_t n);
void dir_free(void *p);
void *move_malloc(size_t n);
void *move_calloc(size_t n, size_t size);
void move_free(void *p);
#endif
