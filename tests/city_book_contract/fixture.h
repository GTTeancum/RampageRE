static void u32(uint8_t*p,uint32_t n,int be){for(int i=0;i<4;i++)p[be?3-i:i]=(uint8_t)(n>>(8*i));}
static void u16(uint8_t*p,uint16_t n,int be){p[be?1:0]=(uint8_t)n;p[be?0:1]=(uint8_t)(n>>8);}
static void fixture(uint8_t f[1024],int wii,int no_book,int parent) {
 memset(f,0,1024);memcpy(f,wii?"Pipeworks Bundle File version 1.4":"Pipeworks Bundle File version 1.3",33);
 u32(f+0x30,wii?1400:1300,wii);u32(f+0x34,0x80,wii);u32(f+0x38,0x180,wii);
 const char*names[]={"Root","Answer","ParentPropertySet","Root:Present"};u32(f+0x80,4,0);unsigned at=20;
 for(int i=0;i<4;i++){u32(f+0x84+4*i,at,0);strcpy((char*)f+0x80+at,names[i]);at+=(unsigned)strlen(names[i])+1;}
 uint8_t*seg=f+0x180;u16(seg+2,1,wii);u32(seg+4,0x240,wii);u32(seg+8,0x280,wii);u32(seg+0x18+6,128,wii);
 f[0x242]=no_book?ET_MATERIAL:ET_BOOK;uint8_t*d=f+0x280;u32(d+0x1C,1,wii);u32(d+0x24,0x30,wii);u32(d+0x30,0x40,wii);
 u32(d+0x40+8,1,wii);u32(d+0x40+16,0x14,wii);
 uint8_t*r=d+0x54;r[0]=parent?PK_NAME:PK_INT;u32(r+4,parent?2:1,wii);u32(r+8,parent?3:42,wii);
}
static City *fresh(const Vol*v){City*c=calloc(1,sizeof*c);assert(c);c->vol=v;strcpy(c->name,"TestCity");return c;}
static void close_city(City*c){city_activate(NULL);city_close(c);}
static uint64_t hash(const void*p,size_t n){uint64_t h=UINT64_C(14695981039346656037);const uint8_t*q=p;while(n--)h=(h^*q++)*UINT64_C(1099511628211);return h;}

