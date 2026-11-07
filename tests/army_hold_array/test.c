#include "source.h"
static void init(ArmyUnit *u,Template *t,float blend) {
    memset(u,0,sizeof *u); u->phase=AU_HELD; u->tmpl=t;
    for(int k=0;k<4;k++) u->pre[k]=u->post[k]=0xaabbccddu;
    const float rot[9]={1,0,0,0,1,0,0,0,1}, pos[3]={5,6,7};
    army_hold_blend_start(&u->hold,rot,pos,blend);
}
static void check(const ArmyUnit *u,const ArmyUnit *v) {
    assert(!memcmp(u->world,v->world,sizeof u->world));
    assert(!memcmp(u->pos,v->pos,sizeof u->pos));
    assert(!memcmp(&u->hold,&v->hold,sizeof u->hold));
    for(int k=0;k<4;k++) assert(u->pre[k]==0xaabbccddu && u->post[k]==0xaabbccddu && v->pre[k]==0xaabbccddu && v->post[k]==0xaabbccddu);
    for(int k=0;k<16;k++) assert(isfinite(u->world[k]));
    for(int k=0;k<3;k++) assert(u->world[12+k]==u->pos[k] && u->world[4*k+3]==0);
    assert(u->world[15]==1);
}
/* A callback-owned status is separate from the Army/Unit state being protected. */
typedef struct { int stopped; unsigned calls; } Status;
static int status_failed(void *ctx) {
    Status *status=ctx;
    assert(status);
    status->calls++;
    return status->stopped;
}
static void status_controls(void) {
    Template t={2,3,1,{{1,2,3},{-4,5,-6}}};
    ArmyUnit u,v;
    init(&u,&t,0.1f); v=u;
    Status status={0,0};
    Army a={.host={.ctx=&status,.failed=status_failed},.n=1,.u=&u};
    Army b={.n=1,.u=&v};
    const float hand[16]={1,0,0,0,0,1,0,0,0,0,1,0,2,3,4,1};
    assert(!host_stopped(NULL));
    assert(!host_stopped(&b.host));
    assert(status.calls==0);
    template_reads=0;
    army_hold(&a,0,hand,0,0.016f);
    assert(status.calls==1 && template_reads>0);
    reference_hold(&b,0,hand,0,0.016f);
    check(&u,&v);

    /* Every nonzero status must stop before any template read or hand dereference.
     * Both the actual flat caller and source-derived reference retain this contract. */
    const int failures[]={1,7,-1};
    for(unsigned k=0;k<sizeof failures/sizeof failures[0];k++) {
        status.stopped=failures[k];
        for(int mirrored=0;mirrored<2;mirrored++) {
            Army saved_army=a;
            ArmyUnit saved_unit=u;
            unsigned before=status.calls;
            template_reads=0;
            army_hold(&a,0,NULL,mirrored,0.25f);
            assert(status.calls==before+1 && template_reads==0);
            assert(!memcmp(&a,&saved_army,sizeof a));
            assert(!memcmp(&u,&saved_unit,sizeof u));
            reference_hold(&a,0,NULL,mirrored,0.25f);
            assert(status.calls==before+2 && template_reads==0);
            assert(!memcmp(&a,&saved_army,sizeof a));
            assert(!memcmp(&u,&saved_unit,sizeof u));
        }
    }
    /* Invalid-unit/phase checks remain no-ops with a live optional status callback. */
    status.stopped=0;
    ArmyUnit saved=u;
    unsigned before=status.calls;
    template_reads=0;
    army_hold(&a,-1,NULL,0,0); army_hold(&a,1,NULL,0,0);
    assert(status.calls==before+2 && template_reads==0);
    assert(!memcmp(&u,&saved,sizeof u));
    u.phase=0; saved=u;
    army_hold(&a,0,NULL,0,0);
    assert(status.calls==before+3 && template_reads==0);
    assert(!memcmp(&u,&saved,sizeof u));
    puts("PASS actual host status: optional/healthy callback, nonzero failure no-mutation and no-template-read controls");
}
int main(void) {
    status_controls();
    unsigned count=0;
    for(int major=-1;major<=3;major++) for(int n=0;n<100;n++) for(int mode=0;mode<7;mode++) for(int mirrored=0;mirrored<2;mirrored++) for(int offsets=0;offsets<3;offsets++) {
        Template t={major, offsets==2?2:3,offsets!=0,{{1,2,3},{-4,5,-6}}};
        ArmyUnit u,v; float blend=mode==0?0:mode==1?0.0001f:0.1f;
        init(&u,&t,blend); v=u; Army a={.n=1,.u=&u}, b={.n=1,.u=&v};
        float dt=mode==2?0:mode==3?0.25f:mode==4?0.1f:mode==5?-0.001f:0.016f;
        for(int step=0;step<12;step++) {
            float angle=(n+step)*0.071f, scale=0.5f+n*0.013f;
            float hand[16]={cosf(angle),sinf(angle),0,0,-sinf(angle)*scale,cosf(angle)*scale,0,0,0,0,scale,0,2+step*0.1f,3,4,1};
            army_hold(&a,0,hand,mirrored,dt); reference_hold(&b,0,hand,mirrored,dt); check(&u,&v); count++;
        }
        ArmyUnit saved=u;
        army_hold(&a,-1,NULL,0,0); army_hold(&a,1,NULL,0,0);
        assert(!memcmp(&u,&saved,sizeof u));
        u.phase=0; saved=u; army_hold(&a,0,NULL,0,0); assert(!memcmp(&u,&saved,sizeof u));
    }
    printf("PASS %u complete caller/reference pairs; exact world/position/blend equality, finite outputs, guards and invalid-call no-ops\n",count);
}
