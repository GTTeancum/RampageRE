static void pf(unsigned char*p,float f){memcpy(p,&f,4);}
static void pu(unsigned char*p,uint32_t v){memcpy(p,&v,4);}
static void ps(unsigned char*p,uint16_t v){memcpy(p,&v,2);}
int main(void){
 unsigned char raw[256]={0}, tr[64][64]={{0}}, rot[64][40]={{0}};
 const uint8_t *raws[64],*trs[64],*rots[64];
 pu(raw+8,2);pu(raw+12,2);pu(raw+0x1c,0x24);pu(raw+0x20,0x60);
 for(int seg=0;seg<2;seg++){
  for(int j=0;j<3;j++){pf(raw+0x24+24*seg+4*j,.3f*j+seg);pf(raw+0x24+24*seg+12+4*j,.2f*j);}
  for(int j=0;j<4;j++){pf(raw+0x60+36*seg+4*j,.13f*(j+1)+seg*.01f);pf(raw+0x60+36*seg+16+4*j,.09f*(j+1));}
  pf(raw+0x60+36*seg+32,seg?-.237f:0);
 }
 TreeNode nodes[64]={0}; Mat4 ma[64],mb[64];float pa[64][7],pb[64][7],held[64][7],faceheld[64][7];uint8_t mask[64];
 for(int i=0;i<64;i++){
  raws[i]=i%11==0?NULL:raw;trs[i]=tr[i];rots[i]=rot[i];
  tr[i][13]=i%5==0?0:3;rot[i][1]=i%7==0?0:3;
  for(int j=0;j<3;j++)pf(tr[i]+4*j,.5f+j*.1f);
  for(int k=0;k<3;k++){
   uint16_t time=k==0?0:k==1?32768:65534;ps(tr[i]+16+8*k,time);ps(rot[i]+2+8*k,time|(k&1));
   for(int j=0;j<3;j++){ps(tr[i]+18+8*k+2*j,(uint16_t)(i*20-k*90+j*50));ps(rot[i]+4+8*k+2*j,(uint16_t)(i*10+k*50+j*80));}
  }
  nodes[i].parent=i==0?-1:i-1;nodes[i].q[3]=1;nodes[i].t[0]=i*.01f;
  for(int j=0;j<7;j++)held[i][j]=faceheld[i][j]=j==6?1:0;
 }
 Clip clips[2]={{.kind=1,.bones=64,.duration=2.5f,.trans=raws},{.kind=2,.bones=64,.duration=3.7f,.trans=trs,.rot=rots}};
 const double times[]={0,-0.0,1.25,2.5,-2.5,5,-5,1000000,1.249999999999,1.250000000001,2.499999999999,2.500000000001,-.000000000001,.000000000001,-1000000};
 unsigned long cases=0,old_calls=0,new_calls=0,samples=0;
 for(int kind=0;kind<2;kind++)for(int duration=0;duration<3;duration++)for(int bone=0;bone<67;bone++)for(unsigned ti=0;ti<sizeof(times)/sizeof(*times);ti++){
  Clip clip=clips[kind];if(duration)clip.duration=duration==1?0:-1;
  float t1[3]={1,2,3},t2[3]={1,2,3},q1[4]={1,2,3,4},q2[4]={1,2,3,4};
  count_fmod=0;int r1=reference_pose(&clip,times[ti],bone,t1,q1);unsigned c1=count_fmod;
  count_fmod=0;int r2=clip_pose(&clip,times[ti],bone,t2,q2);assert(r1==r2&&c1==count_fmod&&!memcmp(t1,t2,sizeof t1)&&!memcmp(q1,q2,sizeof q1));samples++;
 }
 for(int kind=0;kind<4;kind++)for(int mode=0;mode<32;mode++)for(int masking=0;masking<6;masking++)for(unsigned ti=0;ti<sizeof(times)/sizeof(*times);ti++){
  Instance a={0},b={0};Scene sa={0},sb={0};
  a.tree=(Tree){nodes,(mode==31?0:64)};a.clip=clips[kind&1];a.face=clips[kind>>1];
  a.has_clip=!(mode&1);a.has_face=!(mode&2);a.offset=.17f;a.face_t=times[ti]*.25;
  if(mode&4)a.clip.bones=masking==5?0:23;
  if(mode&8)a.face.bones=masking==5?0:17;
  if(mode==28)a.clip.duration=a.face.duration=0;
  if(mode==29)a.clip.duration=a.face.duration=-1;
  a.face_mask=masking==0?NULL:mask;
  for(int i=0;i<64;i++)mask[i]=masking==1?0:masking==2?1:masking==3?i%3==1:masking==4?i>=40:i<2;
  a.held=held;a.face_held=faceheld;a.blend_left=mode&16?.25f:0;a.blend_time=.5f;a.face_blend_left=.2f;a.face_blend_time=.7f;
  a.still_root=mode%8;a.root_hold[0]=.8f;a.root_hold[1]=-.3f;a.root_hold[2]=2;a.root_turn=mode&16?37:0;
  a.nturn=2;a.turn[0].node=5;a.turn[0].axis=2;a.turn[0].angle=.37f;a.turn[1].node=8;a.turn[1].axis=0;a.turn[1].angle=-.12f;
  b=a;a.node=ma;b.node=mb;a.pose=pa;b.pose=pb;
  memset(ma,0xa5,sizeof ma);memset(mb,0xa5,sizeof mb);memset(pa,0xa5,sizeof pa);memset(pb,0xa5,sizeof pb);
  sa.instances=&a;sb.instances=&b;sa.nmon=sb.nmon=mode&16?1:0;
  sa.mon[0].inst=sb.mon[0].inst=0;sa.mon[0].look_neck=sb.mon[0].look_neck=10;sa.mon[0].look_head=sb.mon[0].look_head=11;
  sa.mon[0].look_yaw=sb.mon[0].look_yaw=.17f;sa.mon[0].look_pitch=sb.mon[0].look_pitch=-.3f;
  unsigned expected_old=0,expected_new=0;int body=0,face=0;
  for(unsigned i=0;i<a.tree.n;i++){
   if(a.has_clip&&i<a.clip.bones&&a.clip.duration>0){expected_old++;body=1;}
   if(a.has_face&&a.face_mask&&a.face_mask[i]&&i<a.face.bones&&a.face.duration>0){expected_old++;face=1;}
  }expected_new=body+face;
  count_fmod=0;old_instance(&sa,&a,times[ti]);assert(count_fmod==expected_old);old_calls+=count_fmod;
  count_fmod=0;new_instance(&sb,&b,times[ti]);assert(count_fmod==expected_new);new_calls+=count_fmod;
  assert(!memcmp(ma,mb,sizeof ma)&&!memcmp(pa,pb,sizeof pa));
  a.node=b.node=NULL;a.pose=b.pose=NULL;assert(!memcmp(&a,&b,sizeof a));cases++;
 }
 printf("PASS %lu public-wrapper samples; %lu complete pose_instance cases; fmod %lu -> %lu; bitwise matrices/poses and unchanged remaining instance state\n",samples,cases,old_calls,new_calls);
 return 0;
}
