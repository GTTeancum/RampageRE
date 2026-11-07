static unsigned long cases, baseline_calls, candidate_calls, reduced;
static void check(const CollideObj *o, const float start[3], const float dir[3], float maxt) {
    float ta=123.25f,tb=ta,na[3]={99,98,97},nb[3]={99,98,97};
    calls=0; int a=baseline_ray(o,start,dir,maxt,&ta,na); unsigned ca=calls;
    calls=0; int b=candidate_ray(o,start,dir,maxt,&tb,nb); unsigned cb=calls;
    assert(a==b && !memcmp(&ta,&tb,sizeof ta) && !memcmp(na,nb,sizeof na));
    assert(ca==cb || (ca==3 && cb==1));
    if(ca!=cb) reduced++;
    baseline_calls+=ca;candidate_calls+=cb;cases++;
}
int main(void) {
    static const float starts[][3]={{0,0,0},{0.000999f,0,0},{0.001f,0,0},{0.001001f,0,0},{0.3f,0.4f,0},{-0.4f,0.2f,0.1f},{1,0,0},{1.001f,0,0},{2,2,2},{-2,0,0},{0,1,0},{0,0,-1},{-0.0f,0,-0.0f}};
    static const float dirs[][3]={{1,0,0},{-1,0,0},{0,1,0},{0,0,1},{0,0,0},{1e-13f,0,0},{1,2,3},{-1,-2,-3}};
    static const float radii[]={0,0.000999f,0.001f,0.001001f,0.5f,1,2,10};
    static const float maxs[]={-1,0,0.01f,1,100};
    for(int shape=0;shape<2;shape++) for(int r=0;r<8;r++) for(int tr=0;tr<12;tr++) {
        CollideObj o={0};o.shape.type=shape;o.shape.radius=radii[r];
        for(int k=0;k<3;k++) {o.shape.half[k]=radii[r]*(k+1);o.shape.centre[k]=(tr%3-1)*0.125f;}
        float pos[3]={(tr%4)*1.25f,(tr%3)*-2.25f,(tr%2)*0.5f};
        collide_matrix(o.m,tr*30.0f,pos);
        for(unsigned st=0;st<sizeof starts/sizeof *starts;st++) for(unsigned d=0;d<sizeof dirs/sizeof *dirs;d++) for(int m=0;m<5;m++) {
            float start[3],dir[3],local[3];for(int k=0;k<3;k++) local[k]=starts[st][k]+o.shape.centre[k];
            to_world_dir(o.m,local,start);to_world_dir(o.m,dirs[d],dir);
            for(int k=0;k<3;k++)start[k]+=pos[k];
            check(&o,start,dir,maxs[m]);
        }
    }
    assert(reduced>0 && baseline_calls-candidate_calls==2*reduced);
    printf("PASS %lu exact output cases; %lu reduced-branch cases; sqrtf calls %lu -> %lu\n",cases,reduced,baseline_calls,candidate_calls);
    return 0;
}
