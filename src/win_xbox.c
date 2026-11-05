/* Xbox display and frame lifetime. Desktop GL 1.5 is provided by gl4es on
 * nxdk-gles11, which submits the native renderer's fixed-function work to NV2A.
 * The GL target MUST be compiled with NO_INIT_CONSTRUCTOR: the GLES context and
 * loader callbacks must exist before gl4es probes capabilities. */
#ifdef RAMPAGE_XBOX
#define _WINGDI_                         /* gl4es must not import Win32 WGL */
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include <hal/video.h>
#include <pbkit/pbkit.h>
#include <GL/gl.h>
#include <gl4esinit.h>
#include "render_gl.h"

/* Public nxdk-gles11 functions do not have the GL_APIENTRY convention. The
 * generated loader does, matching gl4esinit.h's callback. */
void glContextInit(GLint width, GLint height);
void glFlipNV2A(void);
void glSwapInterval(int interval);
void *APIENTRY_GL4ES nxdk_gl4es_getprocaddress(const char *name);
void gliStagingDestroy(void);

static int context_live;
static int display_width, display_height;

static void APIENTRY_GL4ES framebuffer_size(int *width, int *height)
{
    *width = display_width;
    *height = display_height;
}

void (*plat_gl_proc(const char *name))(void)
{
    /* Return the DESKTOP wrapper, never a raw GLES entry point: bypassing gl4es
     * would desynchronize its binding, matrix and client-array state. */
    return name && context_live ? (void (*)(void))gl4es_GetProcAddress(name) : NULL;
}

int win_open(Window *w, const char *title, int width, int height, int hidden)
{
    (void)title;
    (void)width;
    (void)height;
    if (!w || context_live)
        return -1;
    memset(w, 0, sizeof *w);
    if (hidden) {
        fprintf(stderr, "Xbox: offscreen window requests are unsupported; capture the displayed frame instead.\n");
        return -1;
    }
    /* A fixed low-memory target works on all retail cable/region combinations.
     * Settings may still contain a desktop resolution; never let that allocate
     * an accidental 1080p framebuffer on the console. */
    display_width = 640;
    display_height = 480;
    if (!XVideoSetMode(display_width, display_height, 32, REFRESH_DEFAULT)) {
        fprintf(stderr, "Xbox: cannot set 640x480x32 display mode.\n");
        return -1;
    }
    if (SDL_InitSubSystem(SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "Xbox: SDL event/timer initialization failed: %s\n", SDL_GetError());
        return -1;
    }
    glContextInit(display_width, display_height);
    set_getprocaddress(nxdk_gl4es_getprocaddress);
    set_getmainfbsize(framebuffer_size);
    initialize_gl4es();
    context_live = 1;
    glSwapInterval(1);
    w->width = display_width;
    w->height = display_height;
    w->focused = 1;
    glViewport(0, 0, display_width, display_height);
    fprintf(stdout, "Xbox renderer: %s; %s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER));
    return 0;
}

/* Match the virtual-key values used by the existing gameplay and menus. The
 * controller path is independent (pad_xbox.c), but USB keyboards remain useful
 * for diagnostics and the game's developer actions. */
static int virtual_key(SDL_Keycode key)
{
    if (key >= SDLK_a && key <= SDLK_z) return 'A' + (int)(key - SDLK_a);
    if (key >= SDLK_0 && key <= SDLK_9) return '0' + (int)(key - SDLK_0);
    if (key >= SDLK_F1 && key <= SDLK_F12) return 0x70 + (int)(key - SDLK_F1);
    switch (key) {
    case SDLK_RETURN: return 0x0D;
    case SDLK_ESCAPE: return 0x1B;
    case SDLK_BACKSPACE: return 0x08;
    case SDLK_TAB: return 0x09;
    case SDLK_SPACE: return 0x20;
    case SDLK_LEFT: return 0x25;
    case SDLK_UP: return 0x26;
    case SDLK_RIGHT: return 0x27;
    case SDLK_DOWN: return 0x28;
    case SDLK_LSHIFT: return 0xA0;
    case SDLK_RSHIFT: return 0xA1;
    case SDLK_LCTRL: return 0xA2;
    case SDLK_RCTRL: return 0xA3;
    case SDLK_LALT: return 0xA4;
    case SDLK_RALT: return 0xA5;
    default: return 0;
    }
}

static void set_key(Window *w, int key, int down)
{
    if (!key) return;
    if (down && !w->keys[key]) w->pressed[key] = 1;
    w->keys[key] = down;
}

void win_poll(Window *w)
{
    SDL_Event e;
    memset(w->pressed, 0, sizeof w->pressed);
    w->mouse_dx = w->mouse_dy = w->wheel = 0;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            w->quit = 1;
            break;
        case SDL_KEYDOWN: case SDL_KEYUP: {
            int down = e.type == SDL_KEYDOWN;
            set_key(w, virtual_key(e.key.keysym.sym), down);
            /* Press either side to produce the generic modifier key; releasing
             * one side must not release the other one. */
            set_key(w, 0x10, w->keys[0xA0] || w->keys[0xA1]);
            set_key(w, 0x11, w->keys[0xA2] || w->keys[0xA3]);
            set_key(w, 0x12, w->keys[0xA4] || w->keys[0xA5]);
            break;
        }
        case SDL_CONTROLLERDEVICEADDED: case SDL_CONTROLLERDEVICEREMOVED:
        case SDL_JOYDEVICEADDED: case SDL_JOYDEVICEREMOVED:
            ++w->dev_changes;
            break;
        default:
            break;
        }
    }
}

void win_swap(Window *w)
{
    (void)w;
    if (context_live) {
        glFlush();                           /* release any gl4es immediate-mode batching first */
        glFlipNV2A();
    }
}

void win_close(Window *w)
{
    if (context_live) {
        glFinish();
        close_gl4es();
        gliStagingDestroy();
        pb_kill();
        context_live = 0;
    }
    SDL_QuitSubSystem(SDL_INIT_TIMER | SDL_INIT_EVENTS);
    if (w) w->focused = 0;
}

void win_title(Window *w, const char *title) { (void)w; (void)title; }

void win_mode(Window *w, int width, int height, int full)
{
    (void)width; (void)height; (void)full;
    if (w && context_live) {
        w->width = display_width;
        w->height = display_height;
    }
}

int win_vsync(Window *w, int interval)
{
    (void)w;
    if (!context_live || (interval != 0 && interval != 1)) return -1;
    glSwapInterval(interval);
    return 0;
}

double win_time(void)
{
    ULONGLONG freq = KeQueryPerformanceFrequency();
    return freq ? (double)KeQueryPerformanceCounter() / (double)freq : 0.0;
}
#endif
