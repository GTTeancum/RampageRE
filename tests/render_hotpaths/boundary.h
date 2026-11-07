#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include "render_gl.h"
#define APIENTRY
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
typedef unsigned GLuint; typedef unsigned GLenum; typedef int GLsizei; typedef int GLint; typedef float GLfloat; typedef unsigned char GLboolean;
#define GL_ALPHA_TEST 1
#define GL_ARRAY_BUFFER 2
#define GL_BLEND 3
#define GL_CLAMP_TO_EDGE 4
#define GL_CLIENT_VERTEX_ARRAY_BIT 5
#define GL_COLOR_ARRAY 6
#define GL_COLOR_BUFFER_BIT 7
#define GL_CULL_FACE 8
#define GL_CURRENT_BIT 9
#define GL_DEPTH_BUFFER_BIT 10
#define GL_DEPTH_TEST 11
#define GL_DST_ALPHA 12
#define GL_ELEMENT_ARRAY_BUFFER 13
#define GL_ENABLE_BIT 14
#define GL_FALSE 0
#define GL_FLOAT 16
#define GL_FOG 17
#define GL_GREATER 18
#define GL_LEQUAL 19
#define GL_LESS 20
#define GL_LIGHTING 21
#define GL_LIGHTING_BIT 22
#define GL_MODELVIEW 23
#define GL_NORMAL_ARRAY 24
#define GL_ONE 25
#define GL_ONE_MINUS_SRC_ALPHA 26
#define GL_POLYGON_BIT 27
#define GL_POLYGON_OFFSET_FILL 28
#define GL_REPEAT 29
#define GL_SMOOTH 30
#define GL_SRC_ALPHA 31
#define GL_TEXTURE 32
#define GL_TEXTURE_2D 33
#define GL_TEXTURE_BIT 34
#define GL_TEXTURE_COORD_ARRAY 35
#define GL_TEXTURE_WRAP_S 36
#define GL_TEXTURE_WRAP_T 37
#define GL_TRIANGLES 38
#define GL_TRUE 1
#define GL_UNSIGNED_INT 40
#define GL_VERTEX_ARRAY 41

static uint64_t trace[32768]; static size_t nt; static unsigned nfmod, npush, ndraw, ntranslate; static float translated[3];
static void put(uint64_t v){ assert(nt<32768); trace[nt++]=v; }
static void tr(const char *s){ uint64_t v=0; while(*s)v=v*131+(unsigned char)*s++; put(v); }
static void vi(uint64_t v){put(v);} static void vf(float f){uint32_t u;memcpy(&u,&f,4);put(u);} static void vd(double f){uint64_t u;memcpy(&u,&f,8);put(u);} static void vp(const void *p){put((uintptr_t)p);}
#define V(x) _Generic((x), float:vf,double:vd,void*:vp,const void*:vp,float*:vp,const float*:vp,uint32_t*:vp,const uint32_t*:vp,default:vi)(x)
static double count_fmod(double a,double b){nfmod++;return fmod(a,b);}
#define fmod count_fmod
#define glEnable(a0) (tr("glEnable"), V(a0), 0)
#define glDisable(a0) (tr("glDisable"), V(a0), 0)
#define glBindTexture(a0,a1) (tr("glBindTexture"), V(a0), V(a1), 0)
#define glTexParameteri(a0,a1,a2) (tr("glTexParameteri"), V(a0), V(a1), V(a2), 0)
#define glMatrixMode(a0) (tr("glMatrixMode"), V(a0), 0)
#define glLoadIdentity() (tr("glLoadIdentity"), 0)
#define glScalef(a0,a1,a2) (tr("glScalef"), V(a0), V(a1), V(a2), 0)
static void glTranslatef(float x,float y,float z){tr("glTranslatef");vf(x);vf(y);vf(z);ntranslate++;translated[0]=x;translated[1]=y;translated[2]=z;}
#define glVertexPointer(a0,a1,a2,a3) (tr("glVertexPointer"), V(a0), V(a1), V(a2), V(a3), 0)
#define glNormalPointer(a0,a1,a2) (tr("glNormalPointer"), V(a0), V(a1), V(a2), 0)
#define glNormal3f(a0,a1,a2) (tr("glNormal3f"), V(a0), V(a1), V(a2), 0)
#define glEnableClientState(a0) (tr("glEnableClientState"), V(a0), 0)
#define glDisableClientState(a0) (tr("glDisableClientState"), V(a0), 0)
#define glTexCoordPointer(a0,a1,a2,a3) (tr("glTexCoordPointer"), V(a0), V(a1), V(a2), V(a3), 0)
#define glColorPointer(a0,a1,a2,a3) (tr("glColorPointer"), V(a0), V(a1), V(a2), V(a3), 0)
#define glColor4f(a0,a1,a2,a3) (tr("glColor4f"), V(a0), V(a1), V(a2), V(a3), 0)
#define glIsEnabled(a0) (tr("glIsEnabled"), V(a0), 1)
#define glBlendFunc(a0,a1) (tr("glBlendFunc"), V(a0), V(a1), 0)
#define glAlphaFunc(a0,a1) (tr("glAlphaFunc"), V(a0), V(a1), 0)
#define glDepthMask(a0) (tr("glDepthMask"), V(a0), 0)
#define glDepthFunc(a0) (tr("glDepthFunc"), V(a0), 0)
#define glDrawElements(a0,a1,a2,a3) (tr("glDrawElements"), V(a0), V(a1), V(a2), V(a3), ++ndraw, 0)
#define glPushAttrib(a0) (tr("glPushAttrib"), V(a0), ++npush, 0)
#define glPopAttrib() (tr("glPopAttrib"), 0)
#define glColorMask(a0,a1,a2,a3) (tr("glColorMask"), V(a0), V(a1), V(a2), V(a3), 0)
#define glDeleteTextures(a0,a1) (tr("glDeleteTextures"), V(a0), V(a1), 0)
#define glFinish() (tr("glFinish"), 0)
#define glPushClientAttrib(a0) (tr("glPushClientAttrib"), V(a0), 0)
#define glPopClientAttrib() (tr("glPopClientAttrib"), 0)
#define glShadeModel(a0) (tr("glShadeModel"), V(a0), 0)
#define glPolygonOffset(a0,a1) (tr("glPolygonOffset"), V(a0), V(a1), 0)
#define glBegin(a0) (tr("glBegin"), V(a0), ++ndraw, 0)
#define glEnd() (tr("glEnd"), 0)
static void glColor4fv(const float *p){ tr("glColor4fv");for(int i=0;i<4;i++)vf(p[i]);}
static void glTexCoord2fv(const float *p){ tr("glTexCoord2fv");for(int i=0;i<2;i++)vf(p[i]);}
static void glVertex3fv(const float *p){ tr("glVertex3fv");for(int i=0;i<3;i++)vf(p[i]);}
