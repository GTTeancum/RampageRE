#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
typedef struct {void *p;size_t n;} Allocation;
static Allocation allocations[4096];
static size_t live_bytes, peak_bytes, live_count, alloc_calls, free_calls, fail_at;
static int find_alloc(void *p) {for(int i=0;i<4096;i++)if(allocations[i].p==p)return i;assert(0);return -1;}
static void track(void *p,size_t n) {if(!p)return;int i=find_alloc(NULL);allocations[i]=(Allocation){p,n};live_count++;live_bytes+=n;if(live_bytes>peak_bytes)peak_bytes=live_bytes;}
static void *tmalloc(size_t n) {alloc_calls++;if(fail_at==alloc_calls)return NULL;void *p=malloc(n?n:1);track(p,n?n:1);return p;}
static void *tcalloc(size_t n,size_t s) {assert(!n||s<=SIZE_MAX/n);void *p=tmalloc(n*s);if(p)memset(p,0,n*s);return p;}
static void tfree(void *p) {if(!p)return;int i=find_alloc(p);live_bytes-=allocations[i].n;live_count--;allocations[i]=(Allocation){0};free_calls++;free(p);}
static void *trealloc(void *p,size_t n) {alloc_calls++;if(fail_at==alloc_calls)return NULL;if(!p){void *q=malloc(n?n:1);track(q,n?n:1);return q;}int i=find_alloc(p);void *q=realloc(p,n?n:1);if(q){live_bytes-=allocations[i].n;live_bytes+=n?n:1;allocations[i]=(Allocation){q,n?n:1};if(live_bytes>peak_bytes)peak_bytes=live_bytes;}return q;}
#define malloc tmalloc
#define calloc tcalloc
#define realloc trealloc
#define free tfree
