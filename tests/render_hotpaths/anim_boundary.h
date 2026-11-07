#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "data.h"
static unsigned calls_fmod, calls_cos, calls_sin;
static double test_fmod(double a, double b) { calls_fmod++; return fmod(a, b); }
static float test_cos(float a) { calls_cos++; return cosf(a); }
static float test_sin(float a) { calls_sin++; return sinf(a); }
#define fmod test_fmod
#define cosf test_cos
#define sinf test_sin
