#include <stdint.h>
#include <stddef.h>
typedef struct {
    size_t pos, n;
    int have, at, hist[2][2], eof, failed;
    double phase;
} Snapshot;
#define DECLARE(p) \
void *p##_new(int, int, int, int); \
void p##_feed(void *, const uint8_t *, size_t); \
int p##_pull(void *, int16_t *, int); \
Snapshot p##_state(void *); \
int p##_block(void *); \
void p##_close(void *); \
void *p##_open(const char *); \
void *p##_ps(const uint8_t *, size_t)
DECLARE(base);
DECLARE(fixed);
