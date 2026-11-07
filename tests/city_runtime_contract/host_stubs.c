#include <stdio.h>
#include "sound.h"
FILE *plat_fopen(const char*p,const char*m){return fopen(p,m);}
int iso_is(FILE*f){(void)f;return 0;}
int iso_find(FILE*f,const char*p,int64_t*o,uint32_t*s){(void)f;(void)p;(void)o;(void)s;return 0;}
int sound_play(const char*n,const float p[3],float v){(void)n;(void)p;(void)v;return 0;}
int sound_effect_start(const char*n,int a){(void)n;(void)a;return -1;}
void sound_effect_set_point(int s,const float p[3]){(void)s;(void)p;}
