#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "ledger.h"
#define CAP (1u<<18)
typedef struct { void *p; size_t n; } Owner;
static Owner owners[CAP];
size_t ledger_bytes, ledger_live, ledger_calls, ledger_fail_at, ledger_fail_hits, ledger_read_calls;
const char *ledger_fail_read_name;
void *__real_malloc(size_t);
void __real_free(void *);
void *__real_realloc(void *,size_t);
static size_t pos(void *p,int insert) {
 size_t k=((uintptr_t)p>>4)*UINT64_C(11400714819323198485)&(CAP-1), tomb=CAP;
 for(size_t n=0;n<CAP;n++,k=(k+1)&(CAP-1)) {
  if(owners[k].p==p)return k;
  if(owners[k].p==(void*)1&&tomb==CAP)tomb=k;
  if(!owners[k].p){assert(insert);return tomb<CAP?tomb:k;}
 }
 assert(insert&&tomb<CAP);return tomb;
}
static int fail(void){ledger_calls++;if(ledger_fail_at==ledger_calls){ledger_fail_hits++;return 1;}return 0;}
void *__wrap_malloc(size_t n){if(fail())return NULL;void*p=__real_malloc(n?n:1);assert(p);size_t k=pos(p,1);owners[k]=(Owner){p,n};ledger_live++;ledger_bytes+=n;return p;}
void *__wrap_calloc(size_t n,size_t m){assert(!m||n<=SIZE_MAX/m);void*p=__wrap_malloc(n*m);if(p)memset(p,0,n*m);return p;}
void __wrap_free(void*p){if(!p)return;size_t k=pos(p,0);ledger_bytes-=owners[k].n;ledger_live--;owners[k]=(Owner){(void*)1,0};__real_free(p);}
void *__wrap_realloc(void*p,size_t n){if(!p)return __wrap_malloc(n);if(!n){__wrap_free(p);return NULL;}if(fail())return NULL;size_t k=pos(p,0),old=owners[k].n;void*q=__real_realloc(p,n);assert(q);owners[k]=(Owner){(void*)1,0};k=pos(q,1);owners[k]=(Owner){q,n};ledger_bytes+=n;ledger_bytes-=old;return q;}
uint8_t *__real_vol_read(const Vol*,const char*,uint32_t*);
uint8_t *__wrap_vol_read(const Vol*v,const char*n,uint32_t*s){ledger_read_calls++;int inject=ledger_fail_read_name&&!strcasecmp(n,ledger_fail_read_name);if(inject){assert(!ledger_fail_at);ledger_fail_at=ledger_calls+1;ledger_fail_read_name=NULL;}uint8_t*r=__real_vol_read(v,n,s);if(inject)ledger_fail_at=0;return r;}
void ledger_reset_fault(void){ledger_fail_at=0;ledger_fail_read_name=NULL;}
void ledger_report(const char*l){printf("{\"label\":\"%s\",\"live\":%zu,\"bytes\":%zu,\"allocation_calls\":%zu,\"read_calls\":%zu,\"injected\":%zu}\n",l,ledger_live,ledger_bytes,ledger_calls,ledger_read_calls,ledger_fail_hits);}
