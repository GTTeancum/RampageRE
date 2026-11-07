#include <stdio.h>
#include "sound.h"
FILE *plat_fopen(const char*p,const char*m){return fopen(p,m);}
int iso_is(FILE*f){(void)f;return 0;}
int iso_find(FILE*f,const char*p,int64_t*o,uint32_t*s){(void)f;(void)p;(void)o;(void)s;return 0;}
int sound_play(const char *name,const float pos[3],float volume){(void)name;(void)pos;(void)volume;return 0;}
int sound_effect_start(const char *name,int auto_stop){(void)name;(void)auto_stop;return -1;}
void sound_effect_set_point(int slot,const float pos[3]){(void)slot;(void)pos;}
