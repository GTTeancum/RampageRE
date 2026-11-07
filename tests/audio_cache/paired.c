#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define DECLARE(n) void n##_prepare(void); const int16_t *n##_load(unsigned,unsigned,size_t*,int*,int*,int*);void n##_discard(void);void n##_done(void);void n##_errors(void);
DECLARE(base)
DECLARE(candidate)
int main(void){
    base_prepare();candidate_prepare();base_errors();candidate_errors();
    size_t cases=0,total_base=0,total_candidate=0,compared=0;
    for(unsigned start_case=0;start_case<63;start_case++)for(unsigned count=0;count<=2048;count++){
        unsigned start=start_case<32?start_case:UINT32_MAX-31+(start_case-32);
        size_t bz,cz;int bn,cn,br,cr,bl,cl;
        const int16_t *b=base_load(start,count,&bz,&bn,&br,&bl);
        const int16_t *c=candidate_load(start,count,&cz,&cn,&cr,&cl);
        unsigned expected=0;for(uint64_t a=start;a<(uint64_t)start+count;a++)expected+=a%16>=2;
        assert(bn==cn&&br==cr&&bl==cl&&cn==(int)expected);
        assert(bz==2u*count&&cz==2u*expected);assert(!memcmp(b,c,cn*sizeof *c));
        compared+=cn;total_base+=bz;total_candidate+=cz;cases++;
        base_discard();candidate_discard();
    }
    base_done();candidate_done();
    printf("PASS paired exact memcmp cases=%zu compared_samples=%zu baseline_sum_alloc=%zu candidate_sum_alloc=%zu saved=%zu; allocation and first/second-read failures retry and free\n",cases,compared,total_base,total_candidate,total_base-total_candidate);
    return 0;
}
