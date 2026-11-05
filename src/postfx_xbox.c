/* The original PS2 game does not require postprocessing. NV2A cannot run the
 * desktop GLSL passes in postfx.c/gtao.c, so report these optional extras absent.
 * Select this file INSTEAD OF postfx.c and gtao.c in the Xbox source list. */
#ifdef RAMPAGE_XBOX
#include "postfx.h"
#include "gtao.h"
void postfx_set(int msaa, int smaa) { (void)msaa; (void)smaa; }
int postfx_msaa_max(void) { return 0; }
int postfx_smaa_supported(void) { return 0; }
void postfx_begin(int w, int h) { (void)w; (void)h; }
void postfx_end(void) {}
int postfx_samples(void) { return 0; }
void gtao_enable(int on, float radius) { (void)on; (void)radius; }
int gtao_supported(void) { return 0; }
int gtao_active(void) { return 0; }
void gtao_frame(float zn, float zf, float sx, float sy, float fog_start, float fog_end)
{ (void)zn; (void)zf; (void)sx; (void)sy; (void)fog_start; (void)fog_end; }
#endif
