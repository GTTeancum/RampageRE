#include "test_api.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned cases;
static const int requests[]={1,2,27,29,55,56,57,513,1024};
static void same_cadence(Snapshot a,Snapshot b)
{
    assert(a.pos==b.pos && a.n==b.n && a.have==b.have && a.at==b.at);
    assert(a.phase==b.phase && a.eof==b.eof && a.failed==b.failed);
}
static void frame(uint8_t *p,int index,int ch,int flag)
{
    p[0]=(uint8_t)(0x14+((index+ch)%4)*16); p[1]=(uint8_t)flag;
    for(int i=2;i<16;i++) p[i]=(uint8_t)(index*19+ch*73+i*17);
}
static void compare(void *old,void *expect,void *candidate,int expect_change)
{
    int16_t a[2048],b[2048],c[2048]; int changed=0,reads=0;
    for(;;) {
        int n=requests[reads++%9]; int na=base_pull(old,a,n),nb=base_pull(expect,b,n),nc=fixed_pull(candidate,c,n);
        assert(na==nb && nb==nc);
        assert(!memcmp(b,c,(size_t)n*4));
        if(memcmp(a,c,(size_t)n*4)) changed=1;
        same_cadence(base_state(old),fixed_state(candidate));
        assert(reads<100000);
        if(!na) break;
    }
    assert(changed==expect_change);
    base_close(old); base_close(expect); fixed_close(candidate); cases++;
}
static void generated(int ch,int inter,int rate,int mode,int end0,int end1,int flag)
{
    uint8_t raw[8192],reference[8192]; int n=ch*inter*4, per=inter/16;
    for(int block=0;block<4;block++) for(int c=0;c<ch;c++) for(int k=0;k<per;k++) {
        int g=block*per+k, end=c?end1:end0, off=(block*ch+c)*inter+16*k;
        frame(raw+off,g,c,g==end?flag:0); memcpy(reference+off,raw+off,16);
        if(mode==0 && flag==1 && end>=0 && g>end) { memset(reference+off,0,16); reference[off]=12; }
    }
    void *a=base_new(ch,inter,rate,mode),*b=base_new(ch,inter,rate,mode),*c=fixed_new(ch,inter,rate,mode);
    /* Every split is passed through the actual packet audio_data accumulator. */
    for(int pos=0;pos<n;) { int z=1+pos%37;if(z>n-pos)z=n-pos;
        base_feed(a,raw+pos,z);base_feed(b,reference+pos,z);fixed_feed(c,raw+pos,z);pos+=z; }
    compare(a,b,c,mode==0&&flag==1&&(end0>=0||(ch==2&&end1>=0)));
}
static void history_and_partial(void)
{
    uint8_t raw[192]; for(int i=0;i<12;i++)frame(raw+16*i,i,i%2,0);
    /* Nonzero predictor-dependent end in each channel's second frame. */
    raw[17]=raw[49]=1;
    void *a=base_new(2,32,48000,0),*c=fixed_new(2,32,48000,0);
    base_feed(a,raw,63);fixed_feed(c,raw,63);
    assert(!base_block(a)&&!fixed_block(c));same_cadence(base_state(a),fixed_state(c));
    assert(!base_state(a).pos); /* An incomplete interleave consumes nothing. */
    base_feed(a,raw+63,1);fixed_feed(c,raw+63,1);
    assert(base_block(a)&&fixed_block(c));Snapshot s=base_state(a),t=fixed_state(c);
    assert(!memcmp(s.hist,t.hist,sizeof s.hist));assert(t.hist[0][0] || t.hist[1][0]);
    fixed_feed(c,raw+64,128);assert(fixed_block(c));assert(fixed_block(c));
    Snapshot later=fixed_state(c);assert(!memcmp(t.hist,later.hist,sizeof t.hist));
    assert(later.pos==192 && later.have==56 && !later.failed);
    base_close(a);fixed_close(c);cases++;
    /* Final partial interleave behavior is deliberately unchanged. */
    a=base_new(2,32,48000,0);void *b=base_new(2,32,48000,0);c=fixed_new(2,32,48000,0);
    base_feed(a,raw,63);base_feed(b,raw,63);fixed_feed(c,raw,63);compare(a,b,c,0);
}
static void put32(uint8_t *p,uint32_t v){for(int i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static size_t pes(uint8_t *dst,const uint8_t *p,size_t n)
{
    size_t size=n+7;assert(size<65536);uint8_t head[]={0,0,1,0xbd,(uint8_t)(size>>8),(uint8_t)size,0x80,0,0,0xff,0xa1,0,0};
    memcpy(dst,head,13);memcpy(dst+13,p,n);return n+13;
}
static void packet_boundaries(void)
{
    uint8_t a[1024],b[1024],head[40]={0},raw[128],ref[128];size_t n=0;
    memcpy(head,"SShd",4);put32(head+4,24);put32(head+8,16);put32(head+12,48000);put32(head+16,2);put32(head+20,32);
    put32(head+24,0xffffffffu);put32(head+28,0xffffffffu);memcpy(head+32,"SSbd",4);put32(head+36,128);
    n+=pes(a+n,head,40);memcpy(b,a,n);
    for(int i=0;i<8;i++)frame(raw+16*i,i,0,i==0||i==2?1:0);
    memcpy(ref,raw,sizeof raw);for(int i=0;i<8;i++)if(i!=0&&i!=2){memset(ref+16*i,0,16);ref[16*i]=12;}
    /* Frame flag/data, left/right boundary, and next-block boundary split. */
    int sizes[]={1,14,1,15,1,1,30,1,3,61};int pos=0;
    for(unsigned i=0;i<sizeof sizes/sizeof *sizes;i++){int z=sizes[i];size_t z1=pes(a+n,raw+pos,z),z2=pes(b+n,ref+pos,z);assert(z1==z2);n+=z1;pos+=z;}
    assert(pos==128);memcpy(a+n,"\0\0\1\xb9",4);memcpy(b+n,"\0\0\1\xb9",4);n+=4;
    compare(base_ps(a,n),base_ps(b,n),fixed_ps(a,n),1);
}
static void compact_after_end(void)
{
    uint8_t raw[64]={0}; raw[0]=raw[32]=12;raw[1]=raw[33]=1;
    void *a=base_new(2,32,48000,0),*b=base_new(2,32,48000,0),*c=fixed_new(2,32,48000,0);
    base_feed(a,raw,64);base_feed(b,raw,64);fixed_feed(c,raw,64);
    int16_t x[112],y[112];assert(base_pull(a,x,56)==56);assert(base_pull(b,x,56)==56);assert(fixed_pull(c,y,56)==56);
    size_t prev=0;int compactions=0;
    for(int i=0;i<18000;i++) {
        for(int k=0;k<4;k++)frame(raw+16*k,i,0,7);
        uint8_t zero[64]={0};for(int k=0;k<4;k++)zero[16*k]=12;
        base_feed(a,raw,64);base_feed(b,zero,64);fixed_feed(c,raw,64);
        assert(base_pull(a,x,56)==56);assert(base_pull(b,x,56)==56);assert(fixed_pull(c,y,56)==56);
        assert(!memcmp(x,y,sizeof x));Snapshot s=fixed_state(c);same_cadence(base_state(a),s);
        if(s.pos<prev)compactions++;prev=s.pos;
    }
    assert(compactions>=1);base_close(a);base_close(b);fixed_close(c);cases++;
}
static void real_movie(const char *path,uint64_t pulse_start,uint64_t expected_frames)
{
    void *a=base_open(path),*c=fixed_open(path);uint64_t at=0,changed=0,prior=0;int16_t x[2048],y[2048];
    Snapshot init=fixed_state(c);assert(!init.hist[0][0]&&!init.hist[1][0]);
    for(;;) {
        int na=base_pull(a,x,1024),nc=fixed_pull(c,y,1024);assert(na==nc);
        same_cadence(base_state(a),fixed_state(c));
        for(int i=0;i<1024;i++)for(int ch=0;ch<2;ch++) {
            uint64_t frame=at+i;int k=2*i+ch;
            if(frame>=pulse_start&&frame<pulse_start+28) { assert(x[k]==28672&&y[k]==0);changed++; }
            else { assert(x[k]==y[k]);if(frame<pulse_start)prior++; }
        }
        at+=(uint64_t)na;if(na<1024)break;
    }
    assert(at==expected_frames && changed==56 && prior==pulse_start*2);
    printf("REAL %s frames=%llu prefix_identical_stereo_frames=%llu changed_samples=%llu old=28672 new=0\n",path,(unsigned long long)at,(unsigned long long)pulse_start,(unsigned long long)changed);
    base_close(a);fixed_close(c);cases++;
}
int main(int argc,char **argv)
{
    for(int ch=1;ch<=2;ch++)for(int inter=16;inter<=1024;inter+=16) {
        int p=inter/16;
        generated(ch,inter,48000,0,p,ch==2?p+1:-1,1);
        generated(ch,inter,48000,0,2*p-1,-1,1);
        generated(ch,inter,48000,0,-1,-1,0);
    }
    int rates[]={24000,44100,48000,96000};
    for(unsigned r=0;r<4;r++)for(int ch=1;ch<=2;ch++)for(int slot=0;slot<2;slot++)
        generated(ch,32,rates[r],0,2+slot,ch==2?5-slot:-1,1);
    for(int mode=1;mode<=6;mode++)generated(2,32,48000,mode,2,3,1);
    for(int flag=0;flag<=255;flag++)if(flag!=1)generated(2,32,48000,0,2,3,flag);
    history_and_partial();packet_boundaries();compact_after_end();
    /* Fresh zeroed instances following ended streams, including no-end data. */
    for(int i=0;i<5;i++){generated(2,32,48000,0,0,1,1);generated(2,32,48000,0,-1,-1,0);}
    if(argc==4) {
        real_movie(argv[1],628852,628880);real_movie(argv[2],240268,240296);real_movie(argv[3],7079100,7079128);
        real_movie(argv[1],628852,628880); /* actual movie_open/close reuse */
    }
    printf("PASS %u cases; actual-source movie audio cases, no emulator or audio device\n",cases);return 0;
}
