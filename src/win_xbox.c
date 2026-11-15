/* Xbox display and frame lifetime. Desktop GL 1.5 is provided by gl4es on
 * nxdk-gles11, which submits the native renderer's fixed-function work to NV2A.
 * The GL target MUST be compiled with NO_INIT_CONSTRUCTOR: the GLES context and
 * loader callbacks must exist before gl4es probes capabilities. */
#ifdef RAMPAGE_XBOX
#ifndef _WINGDI_
#define _WINGDI_                         /* gl4es must not import Win32 WGL */
#endif
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <SDL.h>
#include <hal/video.h>
#include <xboxkrnl/xboxkrnl.h>
#include <pbkit/pbkit.h>
#include <GL/gl.h>
#include <gl4esinit.h>
#include "render_gl.h"
#include "plat.h"

/* Public nxdk-gles11 functions do not have the GL_APIENTRY convention. The
 * generated loader does, matching gl4esinit.h's callback. */
void glContextInit(GLint width, GLint height);
void glFlipNV2A(void);
void glSwapInterval(int interval);
void *APIENTRY_GL4ES nxdk_gl4es_getprocaddress(const char *name);
void gliStagingDestroy(void);
/* the patched pbkit's (platform/xbox/patches/nxdk-pbkit-back-buffers.patch) */
void pb_back_buffers(int n);
void pb_depth_on_boot_screen(int on);
/* the patched nxdk-gles11's (nxdk-gles11-pushbuffer-frames.patch) */
void gliPushbufferSize(uint32_t bytes);
void gliStaticKeepFree(uint32_t bytes);   /* (nxdk-gles11-static-fit.patch) */
int g_xbox_keep_free_kb;                  /* --xkeepfree KB: the store's reserve of free memory (0: by the picture) */

static int context_live;
int g_xbox_video;                        /* 0: by the dashboard's settings. A test build's --xvideo: 480 (4:3), 481
                                          * (480 drawn for a 16:9 screen), 720 */
int g_xbox_two_buffers;                  /* 1280 x 720 with a front and one back buffer (--xon twobuf). Off: with
                                          * two buffers a frame that takes longer than a refresh interval waits
                                          * for the next one, and a console's 1280 x 720 frame of 36 ms was shown
                                          * at 20 a second instead of 26 */
int g_xbox_long_pushbuffer = 1;          /* a 2 MiB push buffer, several frames long, as the game's own Xbox build has
                                          * (--xoff longpb: pbkit's 512 KiB, restarted and the GPU waited for every
                                          * frame) */
static int display_width, display_height;
static int logical_width;                /* the width the game lays its picture out for: 640, or 853 on a 16:9 screen */
int xbox_pixel_width(void) { return display_width; }
int xbox_pixel_height(void) { return display_height; }

/* the GPU has drawn all it was sent: memory it reads may be written again (the posed meshes, src/city.c) */
void xbox_gpu_idle(void)
{
    if (!context_live) return;
    while (pb_busy()) NtYieldExecution();
}

/* pbkit puts the depth buffer in the screen XVideoSetMode allocates (pb_depth_on_boot_screen) only when that screen
 * starts on a 16 KiB boundary, as the GPU's tiled surfaces must; XVideoSetMode asks for 4 KiB. The kernel hands out
 * contiguous memory from the top of what is free, so a few pages taken first put the screen on the boundary: where
 * the screen would land is tried, and the remainder to the boundary is kept as padding (12 KiB at most). On a real
 * console the screen landed off the boundary and 1280 x 720 paid for a fourth screen, 3.5 MiB, that it does not
 * have room for; in xemu it happened to land on it.
 * Called before XVideoSetMode with the mode's size. The picture the last program left for the reboot is given back
 * first, as XVideoSetMode itself does, so that it does not move the screen afterwards. */
static void *boot_pad;
static void boot_screen_align(int width, int height)
{
    if (boot_pad) {
        MmFreeContiguousMemory(boot_pad);
        boot_pad = NULL;
    }
    PVOID previous = AvGetSavedDataAddress();
    if (previous) {
        MmPersistContiguousMemory(previous, MmQueryAllocationSize(previous), FALSE);
        MmFreeContiguousMemory(previous);
        AvSetSavedDataAddress(NULL);
    }
    SIZE_T size = (SIZE_T)width * (SIZE_T)height * 4u;
    void *probe = MmAllocateContiguousMemoryEx(size, 0, 0x7FFFFFFF, 0x1000, PAGE_READWRITE | PAGE_WRITECOMBINE);
    if (!probe)
        return;
    SIZE_T off = (SIZE_T)((uintptr_t)probe & 0x3FFFu);
    MmFreeContiguousMemory(probe);
    if (off)
        boot_pad = MmAllocateContiguousMemoryEx(off, 0, 0x7FFFFFFF, 0x1000, PAGE_READWRITE);
}

static unsigned long used_kb(void)
{
    MM_STATISTICS st;
    memset(&st, 0, sizeof st);
    st.Length = sizeof st;
    if (!NT_SUCCESS(MmQueryStatistics(&st)) || st.AvailablePages > st.TotalPhysicalPages)
        return 0;
    return (unsigned long)(st.TotalPhysicalPages - st.AvailablePages) * 4ul;
}

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
    /* The picture's size comes from the console's own video settings and cable, as for any Xbox game:
     *   1280 x 720  with 720p enabled on the dashboard and an HD (component) cable: always a 16:9 picture
     *   640 x 480   otherwise: interlaced (480i) on the standard cables, progressive (480p) on component with 480p
     *               enabled. With Widescreen set the television stretches the same pixels over a 16:9 screen, so
     *               the picture is drawn for that shape: the game is told a window 853 wide (its camera and HUD
     *               are laid out for it, as for any wide window) and the renderer maps it onto the 640 pixels
     *               (xbox_pixel_width).
     * Settings may still contain a desktop resolution; it never reaches the console.
     * Memory (the stock 64 MB, shared with everything else): three 640 x 480 buffers and the depth buffer are
     * 4.7 MB. At 1280 x 720 each buffer is 3.5 MB, so the picture is drawn with two buffers instead of three
     * (pb_back_buffers: the GPU waits for the television's refresh itself) and with the depth buffer 10.6 MB. */
    display_width = 640;
    display_height = 480;
    int want_720 = g_xbox_video == 720;
    if (!g_xbox_video) {                             /* by the dashboard: is 1280 x 720 among the modes it allows? */
        VIDEO_MODE vm;
        void *at = NULL;
        while (XVideoListModes(&vm, 32, REFRESH_DEFAULT, &at))
            if (vm.width == 1280 && vm.height == 720) want_720 = 1;
    }
    unsigned long before_kb = used_kb();
    if (want_720)
        boot_screen_align(1280, 720);
    if (want_720 && XVideoSetMode(1280, 720, 32, REFRESH_DEFAULT)) {
        display_width = 1280;
        display_height = 720;
    } else {
        boot_screen_align(display_width, display_height);
        if (!XVideoSetMode(display_width, display_height, 32, REFRESH_DEFAULT)) {
            fprintf(stderr, "Xbox: cannot set 640x480x32 display mode.\n");
            return -1;
        }
    }
    if (display_height == 720 && g_xbox_two_buffers)
        pb_back_buffers(1);
    /* XVideoSetMode left a screen of its own allocated that is never shown again: pbkit uses it as the depth buffer
     * instead of taking another (1.2 MB at 640 x 480, 3.5 MB at 1280 x 720) */
    pb_depth_on_boot_screen(1);
    /* The GPU draws a frame while the next is being sent: with pbkit's 512 KiB the buffer is restarted every frame,
     * and a restart waits for the GPU to finish everything first. */
    if (g_xbox_long_pushbuffer)                      /* (1 MiB at 1280 x 720: the third screen buffer needs the room) */
        gliPushbufferSize(display_height == 720 ? 1024u * 1024u : 2u * 1024u * 1024u);
    /* The GPU's store of static meshes stops growing when 8 MiB of RAM are left for what is loaded later. At
     * 1280 x 720 the frame buffers have taken 6 MiB more than at 640 x 480, and what is loaded later is the same:
     * the reserve there is 2.25 MiB, so the store has room for the level's meshes. (3.5 until the Wii's Dallas:
     * its busiest blocks then drew a quarter of their triangles the slow way, their arrays refused, at 20 frames
     * a second; a console went through five block loads and a city's with 2 MiB and no failure. A texture's
     * upload no longer needs room of its own: gliTexturePlaceholderNext.) */
    if (g_xbox_keep_free_kb > 0)
        gliStaticKeepFree((uint32_t)g_xbox_keep_free_kb * 1024u);
    else if (display_height == 720)
        gliStaticKeepFree(2u * 1024u * 1024u + 256u * 1024u);
    if (SDL_InitSubSystem(SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "Xbox: SDL event/timer initialization failed: %s\n", SDL_GetError());
        return -1;
    }
    /* This pinned nxdk-gles11 also initializes gl4es from glContextInit. Install
     * both callbacks first; the explicit initialize below is idempotent and
     * keeps this bootstrap compatible with a GLES-only context initializer. */
    set_getprocaddress(nxdk_gl4es_getprocaddress);
    set_getmainfbsize(framebuffer_size);
    glContextInit(display_width, display_height);
    initialize_gl4es();
    context_live = 1;
    fprintf(stdout, "Xbox video: screen at %p, display memory %lu KB (screens, depth, push buffer, staging)\n",
            (void *)XVideoGetFB(), used_kb() - before_kb);
    glSwapInterval(1);
    int wide = g_xbox_video == 481 || (g_xbox_video != 480 && (XVideoGetEncoderSettings() & VIDEO_WIDESCREEN));
    logical_width = display_height == 720 ? display_width : wide ? 853 : display_width;
    fprintf(stdout, "Xbox video: %d x %d pixels, picture laid out %d wide, %d buffers\n", display_width, display_height,
            logical_width, display_height == 720 && g_xbox_two_buffers ? 2 : 3);
    w->width = logical_width;
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
        w->width = logical_width;
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
