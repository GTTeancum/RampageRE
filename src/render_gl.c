/* render_gl.c - the Win32 window and WGL context (the SDL2 one for the other systems is plat_sdl.c) and an OpenGL 1.1
 * renderer for a Scene (city.c). */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat.h"
#include <GL/gl.h>
#include "render_gl.h"
#include "texpack.h"
#include "postfx.h"
#include "gtao.h"

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

/* ---------------------------------------------------------------- window */

int plat_quiet, plat_told;

int gl_core_fbo(void)
{
    const char *ver = (const char *)glGetString(GL_VERSION), *ext = (const char *)glGetString(GL_EXTENSIONS);
    return (ver && ver[0] >= '3' && ver[0] <= '9') || (ext && strstr(ext, "GL_ARB_framebuffer_object"));
}

/* GL 3.0's (or ARB_framebuffer_object's) framebuffer objects, or EXT_framebuffer_object's (the same enums); a GL that
 * has none fails: the names alone do not tell (glXGetProcAddress hands out a stub for any name) */
int gl_offscreen(int width, int height)
{
    typedef void (APIENTRY *Gen)(GLsizei, GLuint *);
    typedef void (APIENTRY *Bind)(GLenum, GLuint);
    typedef void (APIENTRY *Storage)(GLenum, GLenum, GLsizei, GLsizei);
    typedef void (APIENTRY *Attach)(GLenum, GLenum, GLenum, GLuint);
    typedef GLenum (APIENTRY *Status)(GLenum);
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    const char *sfx = gl_core_fbo() ? "" : ext && strstr(ext, "GL_EXT_framebuffer_object") ? "EXT" : NULL;
    if (!sfx)
        return -1;
    char n[7][40];
    static const char *const base[7] = { "glGenFramebuffers", "glGenRenderbuffers", "glBindFramebuffer",
                                         "glBindRenderbuffer", "glRenderbufferStorage", "glFramebufferRenderbuffer",
                                         "glCheckFramebufferStatus" };
    for (int i = 0; i < 7; i++)
        snprintf(n[i], sizeof n[i], "%s%s", base[i], sfx);
    Gen gen_fb = (Gen)(void *)plat_gl_proc(n[0]);
    Gen gen_rb = (Gen)(void *)plat_gl_proc(n[1]);
    Bind bind_fb = (Bind)(void *)plat_gl_proc(n[2]);
    Bind bind_rb = (Bind)(void *)plat_gl_proc(n[3]);
    Storage storage = (Storage)(void *)plat_gl_proc(n[4]);
    Attach attach = (Attach)(void *)plat_gl_proc(n[5]);
    Status status = (Status)(void *)plat_gl_proc(n[6]);
    if (!gen_fb || !gen_rb || !bind_fb || !bind_rb || !storage || !attach || !status)
        return -1;
    GLuint fb, rb[2];
    gen_fb(1, &fb);
    gen_rb(2, rb);
    bind_fb(0x8D40, fb);                                   /* GL_FRAMEBUFFER */
    bind_rb(0x8D41, rb[0]);                                /* GL_RENDERBUFFER */
    storage(0x8D41, 0x8058, width, height);                /* GL_RGBA8 */
    attach(0x8D40, 0x8CE0, 0x8D41, rb[0]);                 /* GL_COLOR_ATTACHMENT0 */
    bind_rb(0x8D41, rb[1]);
    storage(0x8D41, 0x88F0, width, height);                /* GL_DEPTH24_STENCIL8 */
    attach(0x8D40, 0x8D00, 0x8D41, rb[1]);                 /* GL_DEPTH_ATTACHMENT */
    attach(0x8D40, 0x8D20, 0x8D41, rb[1]);                 /* GL_STENCIL_ATTACHMENT */
    return status(0x8D40) == 0x8CD5 ? 0 : -1;              /* GL_FRAMEBUFFER_COMPLETE */
}

#if defined(_WIN32) && !defined(RAMPAGE_XBOX)
static Window *g_win;

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Window *w = g_win;
    static int last_x, last_y;
    switch (msg) {
    case WM_CLOSE: if (w) w->quit = 1; return 0;
    case WM_SIZE: if (w) { w->width = LOWORD(lp); w->height = HIWORD(lp); } return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if (w && wp < 256) { if (!w->keys[wp]) w->pressed[wp] = 1; w->keys[wp] = 1; }
        if (w && msg == WM_SYSKEYDOWN && wp == VK_F4)
            w->quit = 1;                             /* Alt+F4: as the window's close (WM_CLOSE) */
        return 0;
    case WM_KEYUP: case WM_SYSKEYUP: if (w && wp < 256) w->keys[wp] = 0; return 0;
    case WM_SYSCHAR: if (wp == '\r') return 0; break;   /* Alt+Enter (fullscreen, settings_keys): no beep */
    case WM_RBUTTONDOWN: if (w) { w->mouse_right = 1; last_x = (short)LOWORD(lp); last_y = (short)HIWORD(lp); SetCapture(hwnd); } return 0;
    case WM_RBUTTONUP: if (w) w->mouse_right = 0; ReleaseCapture(); return 0;
    case WM_MOUSEMOVE:
        if (w && w->mouse_right) {
            int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
            w->mouse_dx += x - last_x; w->mouse_dy += y - last_y;
            last_x = x; last_y = y;
        }
        return 0;
    case WM_MOUSEWHEEL: if (w) w->wheel += GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA; return 0;
    case WM_SETFOCUS: if (w) w->focused = 1; break;
    case WM_KILLFOCUS: if (w) { w->focused = 0; memset(w->keys, 0, sizeof w->keys); } return 0;
    case WM_DEVICECHANGE: if (w) w->dev_changes++; break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* hidden: never shown nor focused (--shot); the frame goes to an offscreen framebuffer (gl_offscreen) of width x
 * height, or, without one, to the window shown without taking the focus, off the screen */
int win_open(Window *w, const char *title, int width, int height, int hidden)
{
    memset(w, 0, sizeof *w);
    g_win = w;
    HINSTANCE inst = GetModuleHandleA(NULL);
    WNDCLASSA wc = { 0 };
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "RampageRE";
    RegisterClassA(&wc);
    RECT rc = { 0, 0, width, height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExA(hidden ? WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW : 0, "RampageRE", title,
                                WS_OVERLAPPEDWINDOW | (hidden ? 0 : WS_VISIBLE), hidden ? -32000 : CW_USEDEFAULT,
                                hidden ? -32000 : CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, NULL, NULL,
                                inst, NULL);
    if (!hwnd)
        return -1;
    HDC hdc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = { sizeof pfd, 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA,
                                  32, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 24, 8, 0, PFD_MAIN_PLANE, 0, 0, 0, 0 };
    int pf = ChoosePixelFormat(hdc, &pfd);
    if (!pf || !SetPixelFormat(hdc, pf, &pfd))
        return -1;
    HGLRC rc_gl = wglCreateContext(hdc);
    if (!rc_gl || !wglMakeCurrent(hdc, rc_gl))
        return -1;
    typedef BOOL (WINAPI *SwapInterval)(int);
    SwapInterval swap = (SwapInterval)(void *)wglGetProcAddress("wglSwapIntervalEXT");
    if (swap)
        swap(1);
    w->hwnd = hwnd; w->hdc = hdc; w->hglrc = rc_gl;
    w->width = width; w->height = height;
    w->hidden = hidden;
    if (hidden && gl_offscreen(width, height) != 0) {
        SetWindowPos(hwnd, HWND_BOTTOM, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    }
    return 0;
}

void win_poll(Window *w)
{
    MSG msg;
    memset(w->pressed, 0, sizeof w->pressed);
    w->mouse_dx = w->mouse_dy = w->wheel = 0;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

void win_swap(Window *w) { SwapBuffers((HDC)w->hdc); }
void win_title(Window *w, const char *title) { SetWindowTextA((HWND)w->hwnd, title); }

/* port extra (WoodyRE's): borderless fullscreen over the window's monitor, or a window centred on its work area */
void win_mode(Window *w, int width, int height, int full)
{
    if (w->hidden)                               /* --shot: never shown, its size is the framebuffer object's */
        return;
    HWND h = (HWND)w->hwnd;
    MONITORINFO mi = { sizeof mi };
    GetMonitorInfoA(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
    if (full) {
        SetWindowLongA(h, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(h, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        return;
    }
    RECT rc = { 0, 0, width, height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int fw = rc.right - rc.left - width, fh = rc.bottom - rc.top - height;
    int aw = mi.rcWork.right - mi.rcWork.left - fw, ah = mi.rcWork.bottom - mi.rcWork.top - fh;
    if (width > aw || height > ah) {             /* too big for the screen: shrunk, its shape kept */
        float s = (float)aw / width < (float)ah / height ? (float)aw / width : (float)ah / height;
        printf("window %dx%d does not fit the screen: %dx%d\n", width, height, (int)(width * s), (int)(height * s));
        width = (int)(width * s);
        height = (int)(height * s);
    }
    SetWindowLongA(h, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    SetWindowPos(h, HWND_NOTOPMOST, mi.rcWork.left + (aw - width) / 2, mi.rcWork.top + (ah - height) / 2, width + fw,
                 height + fh, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

int win_vsync(Window *w, int interval)
{
    typedef BOOL (WINAPI *SwapInterval)(int);
    SwapInterval swap = (SwapInterval)(void *)wglGetProcAddress("wglSwapIntervalEXT");
    return !w->hidden && swap && swap(interval) ? 0 : -1;
}

void win_close(Window *w)
{
    wglMakeCurrent(NULL, NULL);
    if (w->hglrc) wglDeleteContext((HGLRC)w->hglrc);
    if (w->hwnd) DestroyWindow((HWND)w->hwnd);
    g_win = NULL;
}

double win_time(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER now;
    if (!freq.QuadPart)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
}
#endif

/* ---------------------------------------------------------------- renderer */

/* a texture region's own texture (region_texture): the tile of texture `name` that the region reg picks, 0 = the
 * whole texture; the texture matrix's scale for it */
typedef struct { uint32_t name; float reg[4]; GLuint id; float scale[2]; } RegionTex;
typedef struct {
    const Bundle *b; GLuint *tex, *hud_tex; uint32_t n;            /* per name index: GL texture, ~0 = none */
    RegionTex *reg; int nreg;
#ifdef RAMPAGE_XBOX
    uint8_t *not_ifl;                                /* per name index: 1 = not an .ifl (shown_texture) */
#endif
} BundleTex;

/* an animated texture (.ifl, a text entry: per line a frame's image and how many frames it shows, 3ds Max's 30 per
 * second): its frames' texture names */
#define IFL_FRAMES 64
typedef struct { const Bundle *b; uint32_t name; int n; uint32_t frame[IFL_FRAMES]; float until[IFL_FRAMES]; } Ifl;

/* a blended part held back while the ambient occlusion is on (rnd_frame): drawn after it with the modelview it had */
typedef struct { float mv[16]; const SceneMesh *sm; const Skin *skin; uint32_t part, first; } HeldPart;
typedef struct { const MeshPart *parts; uint32_t nparts; float c[3], r; } MeshBound;   /* a mesh's sphere (item_seen) */
/* a part's arrays in GL buffers (part_buffers): the vertices' pos, nrm, uv, col one after the other (at those byte
 * offsets, 0 = none but pos), the triangles' indices in ibo; what they were made from, to tell when to make them again */
typedef struct {
    GLuint vbo, ibo;
    uint32_t rev, nverts, ntris;
    const float *pos, *nrm, *uv, *col; const uint32_t *tris;
    size_t at_nrm, at_uv, at_col;
#ifdef RAMPAGE_XBOX
    uint16_t *idx16;                     /* the triangles as 16-bit indices (xbox_part), NULL: more than 65536 vertices */
    uint8_t churn;                       /* (a posed part) times its registered arrays changed: past 3 staged */
    uint8_t wait;                        /* (a posed part) draws before its arrays are registered (vbo 3: waiting) */
    uint8_t skinned;                     /* drawn posed: its positions and normals are never registered */
    uint8_t own;                         /* vbo 1 and its arrays in GPU memory are its own copies (xbox_own_build),
                                          * not registered by the part's addresses */
    uint8_t stale;                       /* the part changed since its copies were made: they are made again when
                                          * the frame's budget allows, the old ones are drawn until then */
    uint32_t nidx16;                     /* the indices in idx16 (those of the copies: the part may have changed) */
    uint32_t stale_since;                /* the frame it changed in while its copies were current */
    uint8_t baked;                       /* its copies' texture coordinates hold the material's region scale (rs):
                                          * drawn without the texture matrix, which only gl4es could apply */
    float rs[2];
    const float *gpos, *gnrm, *guv, *gcol;       /* its arrays in GPU memory */
    struct SkinGpu *sg;                  /* a skinned part as the GPU poses it (xbox_skin_gpu) */
#endif
} PartBuf;
typedef struct { const MeshPart *parts; uint32_t nparts; PartBuf *pb; } MeshBuf;
#ifdef RAMPAGE_XBOX
/* the batches of the standing city (bat_frame) */
typedef struct { GLuint tex; float col[4], amb[3]; uint8_t fmt, two_sided, clamp, uncut, own, blend; } BatKey;   /* blend: 0 opaque,
                                                      * 1 the cracks, 2 a blended sheet, 3 see-through by its colour */
typedef struct { uint32_t item, i0, ni, nv; uint16_t part, gone; } BatMember;   /* its indices: b->idx[i0 .. i0 + ni);
                                                      * gone: it left, its triangles are no longer sent (its place
                                                      * in the arrays is given up when the batch is next built) */
typedef struct {
    float *pos, *nrm, *uv, *col; uint16_t *idx;
    uint32_t nverts, nidx, cidx;                 /* built; the indices' capacity */
    BatMember *mem; uint32_t nmem, cmem;         /* its members */
    uint32_t fverts, fidx;                       /* their vertices and indices */
    int registered, failed, dirty;               /* dirty: its members changed, built again at its next draw */
} BatSub;
typedef struct { BatKey key; BatSub *sub; int nsub; } BatGroup;
typedef struct {
    const MeshPart *parts; uint32_t nparts, mesh, settle, moves, nkeys;
    float world[16], amb[4];
    int16_t *key;                                /* per part: its group, -1 drawn by draw_part */
    int16_t *sub;                                /* per part: the batch of its group it is a member of, -1 none */
    uint32_t *rev, *out;                         /* per part: its MeshPart.rev; the frame it comes back (0: in) */
    uint8_t *chg;                                /* per part: times it changed (it stays out longer each time) */
    uint8_t seen, sorted, active;
} BatItem;
#endif

struct Renderer {
    const Scene *scene;
    void (*output_pump)(void);
    double output_pump_at;
    BundleTex cache[16]; int ncache;
    Ifl ifl[32]; int nifl;
    uint32_t drawn;
    const uint8_t *video; int video_w, video_h, video_new;   /* rnd_video */
    GLuint video_tex;
    int video_tex_w, video_tex_h;                 /* allocate once, update subsequent FMV frames in place */
    int tex_gen;                                     /* the texture settings the cache was made with */
    int hold;                                        /* draw_mesh holds the blended parts back (gtao_active) */
    double hud_t; int has_hud_t;                     /* the GUI's clock (rnd_hud_time), else the frame's */
    int in_hud_quad;                              /* 2D quad texture upload, not the GUI's 3D meshes */
    int in_hud;                                      /* draw_hud: textures made now get no mips nor anisotropy */
    HeldPart *held; int nheld, cheld;
    MeshBound *bound; uint32_t nbound;               /* by Scene.meshes index, made at its first draw */
    MeshBuf *mbuf; uint32_t nmbuf;                   /* by Scene.meshes index, made at its first draw (part_buffers) */
    float *stage; size_t nstage;                     /* a part's arrays laid out for its buffer */
    float *water_uv; size_t nwater_uv;              /* shared water draw scratch; renderer-owned high-water capacity */
#ifdef RAMPAGE_XBOX
    BatGroup *bgroup; uint32_t nbgroup;
    BatItem *bitem; uint32_t nbitem;                 /* by Scene.items index */
    const int16_t *bat_keys;                         /* the item draw_mesh is drawing: its batched parts */
    const Instance *cur_inst;                        /* its instance (the bones of a skinned one) */
    const Instance *pal_inst; uint32_t pal_frame, xframe, npal; Mat4 *pal;   /* that instance's palette, this frame's */
    struct RigMesh *rig; int nrig;                   /* the moving models' merged meshes (rig_mesh) */
    uint32_t own_budget, own_starved, own_made;      /* xbox_part: bytes of changed parts' copies still to be made
                                                      * this frame (and for the ones kept waiting), and how many were */
    float bat_amb[3], bat_amb_now[3]; int bat_own;   /* the scene's ambient; the one set while the batches draw */
    uint16_t *bat_idx; uint32_t cbat_idx;            /* a batch's indices of the items in sight this frame */
#endif
};

/* ---- texture settings (port extra, docs/NATIVE.md "Graphics settings") */
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
static int g_aniso = 1, g_mips, g_tex_gen, g_aniso_max = -1;

int rnd_aniso_max(void)
{
    if (g_aniso_max < 0) {
        const char *e = (const char *)glGetString(GL_EXTENSIONS);
        GLfloat m = 1;
        if (e && (strstr(e, "GL_EXT_texture_filter_anisotropic") || strstr(e, "GL_ARB_texture_filter_anisotropic")))
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &m);
        g_aniso_max = m >= 16 ? 16 : m >= 8 ? 8 : m >= 4 ? 4 : m >= 2 ? 2 : 1;
    }
    return g_aniso_max;
}

void rnd_set_textures(int aniso, int mips)
{
    aniso = aniso < 1 ? 1 : aniso;
    mips = mips != 0;
    if (aniso != g_aniso || mips != g_mips)
        g_tex_gen++;
    g_aniso = aniso;
    g_mips = mips;
}

/* the bound texture's anisotropy, at most what the GL can */
static void tex_aniso(void)
{
    int m = g_aniso > 1 ? rnd_aniso_max() : 1;
    if (m > 1)
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, (GLfloat)(g_aniso < m ? g_aniso : m));
}

/* the levels below rgba (w x h, level 0 already uploaded) down to 1 x 1, each a 2x2 box of the one above, its colour
 * weighted by alpha (so the see-through texels' colour does not bleed into the cut-outs' edges), trilinear filtering */
static void upload_mips(const uint8_t *rgba, int w, int h)
{
    uint8_t *px = malloc((size_t)w * h * 4);
    if (!px)
        return;
    memcpy(px, rgba, (size_t)w * h * 4);
    int level = 0;
    while (w > 1 || h > 1) {
        int dw = w > 1 ? w / 2 : 1, dh = h > 1 ? h / 2 : 1;
        for (int y = 0; y < dh; y++)
            for (int x = 0; x < dw; x++) {
                int x0 = 2 * x, x1 = 2 * x + 1 < w ? 2 * x + 1 : x0, y0 = 2 * y, y1 = 2 * y + 1 < h ? 2 * y + 1 : y0;
                const uint8_t *s[4] = { px + ((size_t)y0 * w + x0) * 4, px + ((size_t)y0 * w + x1) * 4,
                                        px + ((size_t)y1 * w + x0) * 4, px + ((size_t)y1 * w + x1) * 4 };
                uint8_t *d = px + ((size_t)y * dw + x) * 4;   /* in place: d never passes the sources still to be read */
                unsigned c[3] = { 0, 0, 0 }, cs[3] = { 0, 0, 0 }, a = 0;
                for (int k = 0; k < 4; k++) {
                    for (int q = 0; q < 3; q++) {
                        c[q] += s[k][q] * s[k][3];
                        cs[q] += s[k][q];
                    }
                    a += s[k][3];
                }
                for (int q = 0; q < 3; q++)
                    d[q] = (uint8_t)(a ? (c[q] + a / 2) / a : (cs[q] + 2) / 4);
                d[3] = (uint8_t)((a + 2) / 4);
            }
        w = dw;
        h = dh;
        glTexImage2D(GL_TEXTURE_2D, ++level, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    }
    free(px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
}

Renderer *rnd_create(void) { return calloc(1, sizeof(Renderer)); }

/* Same-thread output service only: callback must not advance gameplay or call GL.
 * Bound callback rate while allowing long draw/upload batches to refill audio. */
void rnd_set_output_pump(Renderer *r, void (*pump)(void))
{
    if (!r) return;
    r->output_pump = pump;
    r->output_pump_at = 0;
}
static void pump_output(Renderer *r)
{
    if (!r->output_pump) return;
#ifdef RAMPAGE_XBOX
    /* Called for every part drawn. Reading the console's clock is a kernel call and a port read, and several
     * hundred parts a frame made that alone milliseconds: look at the clock every 16th part. */
    static unsigned calls;
    if (++calls & 15u) return;
#endif
    double now = win_time();
    if (now >= r->output_pump_at && now - r->output_pump_at < 0.016) return;
    r->output_pump_at = now;
    r->output_pump();
}


static void drop_textures(Renderer *r)
{
#ifdef RAMPAGE_XBOX
    /* nxdk frees contiguous texture storage immediately. Flush gl4es' deferred
     * draws and wait for NV2A before retiring resources at a scene transition. */
    glFinish();
#endif
    for (int k = 0; k < r->ncache; k++) {
        for (uint32_t i = 0; r->cache[k].tex && i < r->cache[k].n; i++)
            if (r->cache[k].tex[i] && r->cache[k].tex[i] != ~0u)
                glDeleteTextures(1, &r->cache[k].tex[i]);
        for (uint32_t i = 0; r->cache[k].hud_tex && i < r->cache[k].n; i++)
            if (r->cache[k].hud_tex[i] && r->cache[k].hud_tex[i] != ~0u)
                glDeleteTextures(1, &r->cache[k].hud_tex[i]);
        for (int i = 0; i < r->cache[k].nreg; i++)
            if (r->cache[k].reg[i].id)
                glDeleteTextures(1, &r->cache[k].reg[i].id);
        free(r->cache[k].tex);
        free(r->cache[k].hud_tex);
        free(r->cache[k].reg);
#ifdef RAMPAGE_XBOX
        free(r->cache[k].not_ifl);
#endif
    }
    r->ncache = 0;
    r->nifl = 0;
}

/* ---- port extra: the unskinned parts' arrays in GL buffer objects (GL 1.5 or ARB_vertex_buffer_object; ES 2.0 has
 * them, gles2.c tracks them), uploaded once instead of the driver copying the client arrays at every draw */
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#endif
typedef void (APIENTRY *BufGen)(GLsizei, GLuint *);
typedef void (APIENTRY *BufDel)(GLsizei, const GLuint *);
typedef void (APIENTRY *BufBind)(GLenum, GLuint);
typedef void (APIENTRY *BufData)(GLenum, ptrdiff_t, const void *, GLenum);
static struct { int tried; BufGen gen; BufDel del; BufBind bind; BufData data; } g_buf;

/* the buffer calls, 0 when the GL has none (the client arrays then, as before) */
static int buffers_ok(void)
{
#ifdef RAMPAGE_XBOX
    /* gl4es keeps a CPU shadow of each buffer, in addition to the scene arrays and
     * NV2A copy. Start with client arrays to stay within stock Xbox's 64 MB shared
     * RAM; nxdk-gles11 stages these in its bounded DMA arena. An opt-in preserves
     * the VBO path for measuring larger-memory/debug machines. */
    const char *vbo = getenv("RAMPAGE_XBOX_VBO");
    if (!vbo || *vbo != '1')
        return 0;
#endif
    if (!g_buf.tried) {
        g_buf.tried = 1;
        const char *sfx = "", *off = getenv("RAMPAGE_NOVBO");   /* 1: the client arrays (to compare) */
        if (off && *off && *off != '0')
            return 0;
#ifndef RAMPAGE_GLES
        const char *ver = (const char *)glGetString(GL_VERSION), *ext = (const char *)glGetString(GL_EXTENSIONS);
        int core = ver && ver[0] >= '1' && ver[0] <= '9' && (ver[0] > '1' || (ver[1] == '.' && ver[2] >= '5'));
        sfx = core ? "" : ext && strstr(ext, "GL_ARB_vertex_buffer_object") ? "ARB" : NULL;
        if (!sfx)
            return 0;
#endif
        char n[4][32];
        static const char *const base[4] = { "glGenBuffers", "glDeleteBuffers", "glBindBuffer", "glBufferData" };
        for (int i = 0; i < 4; i++)
            snprintf(n[i], sizeof n[i], "%s%s", base[i], sfx);
        g_buf.gen = (BufGen)(void *)plat_gl_proc(n[0]);
        g_buf.del = (BufDel)(void *)plat_gl_proc(n[1]);
        g_buf.bind = (BufBind)(void *)plat_gl_proc(n[2]);
        g_buf.data = (BufData)(void *)plat_gl_proc(n[3]);
        if (!g_buf.gen || !g_buf.del || !g_buf.bind || !g_buf.data)
            g_buf.gen = NULL;
    }
    return g_buf.gen != NULL;
}

#ifdef RAMPAGE_XBOX
/* nxdk-gles11's static client arrays (platform/xbox/patches/nxdk-gles11-static-arrays.patch): an unskinned scene
 * part's arrays are copied once into memory the NV2A reads and drawn from there, in place of the copy into the
 * staging arena at every draw. gl4es has no buffers on its ES 1.1 backend, so the arrays stay client arrays to it
 * and the registration is by their addresses; PartBuf.vbo is 1 while a part's are registered, 2 when the store
 * refused them or the part is skinned (its arrays are posed every frame; not tried again until the part changes).
 * Every part also keeps its triangles as 16-bit indices: gl4es converts 32-bit ones on every draw, and the NV2A
 * takes inline indices at half the push-buffer bytes. */
unsigned char gliStaticArrayAdd(const void *src, size_t bytes, const void *owner);
void gliStaticArrayRemove(const void *src, const void *owner);
void *gliStaticArrayAlloc(size_t bytes, const void *owner);
void gliStaticArraysHint(unsigned char on);
/* The draws that do not go through gl4es (platform/xbox/patches/nxdk-gles11-direct-draw.patch): arrays that are
 * in GPU memory are named to the NV2A by their addresses and drawn, the GL state as it stands. A draw through
 * glDrawElements crossed gl4es (which copies small draws into its own batches) and the backend's client arrays,
 * tens of microseconds each whatever its size. gl4es is asked first (gl4es_xbox_predraw: its pending texture work
 * is done; 0 when the draw needs gl4es after all, a texture whose coordinates it rewrites). gliSkinDraw poses a
 * skinned mesh on the GPU with a vertex program (platform/xbox/shaders/skin.vs.cg). */
const void *gliStaticArrayAddress(const void *src);
void gliDirectDraw(const float *pos, const float *nrm, const float *uv, const float *col, const uint16_t *idx, int count);
int gliSkinMaxBones(void);
void gliSkinNewFrame(void);
void gliSkinDraw(const float *pos, const float *nrm, const float *uv, const float *col, const int16_t *bone,
                 const float *weight, const uint16_t *idx, int count, const float (*palette)[12], int nbones,
                 int four);
int gl4es_xbox_predraw(void);
extern int g_xbox_direct_quads, g_xbox_own_parts, g_xbox_region_bake, g_xbox_gpu_skin, g_xbox_item_skip;
extern int g_xbox_preload, g_xbox_rigid;

/* A skinned part for the GPU: its triangles in groups that each use at most the vertex program's bones, a group's
 * vertices (the bind pose, with each one's bones as indices into the group's list, and their weights) in the static
 * store. Made once for a mesh part and shared by every instance drawn with it. */
#define SKIN_GROUP_BONES 26
#define SKIN_GROUP_VERTS 16000u                  /* (16 bytes a vertex in the largest array, a 256 KB chunk) */
typedef struct {
    float *pos, *nrm, *uv, *col, *weight;
    int16_t *bone;                               /* per vertex 4 shorts: 3 x the bone's place in b (the program's
                                                  * constant offset, as the prototype stores it) */
    uint16_t *idx;
    uint32_t nverts, nidx;
    uint8_t nb, b[SKIN_GROUP_BONES];             /* its bones: indices into the instance's palette */
    uint8_t four;                                /* a vertex with more than two bones: the four-bone program */
} SkinGroup;
typedef struct SkinGpu { SkinGroup *g; int ng, failed; } SkinGpu;

static void skin_gpu_free(SkinGpu *sg)
{
    if (!sg)
        return;
    for (int k = 0; k < sg->ng; k++) {
        SkinGroup *g = &sg->g[k];
        if (g->pos) gliStaticArrayRemove(g->pos, sg);
        if (g->nrm) gliStaticArrayRemove(g->nrm, sg);
        if (g->uv) gliStaticArrayRemove(g->uv, sg);
        if (g->col) gliStaticArrayRemove(g->col, sg);
        if (g->bone) gliStaticArrayRemove(g->bone, sg);
        if (g->weight) gliStaticArrayRemove(g->weight, sg);
        free(g->idx);
    }
    free(sg->g);
    free(sg);
}

static void xbox_static_unregister(PartBuf *pb)
{
    skin_gpu_free(pb->sg);
    pb->sg = NULL;
    if (pb->vbo == 1 && pb->own) {                   /* (released copies are freed once the GPU is idle) */
        if (pb->gpos) gliStaticArrayRemove(pb->gpos, pb);
        if (pb->gnrm) gliStaticArrayRemove(pb->gnrm, pb);
        if (pb->guv) gliStaticArrayRemove(pb->guv, pb);
        if (pb->gcol) gliStaticArrayRemove(pb->gcol, pb);
    } else if (pb->vbo == 1) {
        gliStaticArrayRemove(pb->pos, pb);
        if (pb->nrm) gliStaticArrayRemove(pb->nrm, pb);
        if (pb->uv) gliStaticArrayRemove(pb->uv, pb);
        if (pb->col) gliStaticArrayRemove(pb->col, pb);
    }
    pb->gpos = pb->gnrm = pb->guv = pb->gcol = NULL;
    pb->vbo = 0;
    pb->own = pb->stale = pb->baked = 0;
    free(pb->idx16);
    pb->idx16 = NULL;
    pb->nidx16 = 0;
}

/* An unposed part's own copies in GPU memory, as the Xbox prototype keeps a building's walls (docs/xbox_proto/
 * WORLD_DRAWING.md: buffers of its own, damage only marks them, they are rewritten on a budget and the old ones are
 * drawn meanwhile). The part's arrays are copied into the static store under the store's own addresses, so a facade
 * that city.c frees and builds again at another (or a reused) address cannot answer for another part; the copies made
 * before are released (freed when the GPU is idle: a frame still queued may be reading them) and their 16-bit
 * indices go with them. 0: no room (what was there stays). */
#define OWN_STARVED_BYTES (96u * 1024u)             /* a frame's budget for the changed parts kept waiting */
static int xbox_own_build(PartBuf *pb, const MeshPart *p, const float *rs)
{
    size_t n = p->nverts, ni = 3 * (size_t)p->ntris;
    if (!n || !ni || n > 65536 || !p->pos || !p->tris)
        return 0;
    uint16_t *idx = malloc(sizeof *idx * ni);
    float *pos = gliStaticArrayAlloc(sizeof(float) * 3 * n, pb);
    float *nrm = p->nrm ? gliStaticArrayAlloc(sizeof(float) * 3 * n, pb) : NULL;
    float *uv = p->uv ? gliStaticArrayAlloc(sizeof(float) * 2 * n, pb) : NULL;
    float *col = p->col ? gliStaticArrayAlloc(sizeof(float) * 4 * n, pb) : NULL;
    if (!idx || !pos || (p->nrm && !nrm) || (p->uv && !uv) || (p->col && !col)) {
        if (pos) gliStaticArrayRemove(pos, pb);
        if (nrm) gliStaticArrayRemove(nrm, pb);
        if (uv) gliStaticArrayRemove(uv, pb);
        if (col) gliStaticArrayRemove(col, pb);
        free(idx);
        return 0;
    }
    memcpy(pos, p->pos, sizeof(float) * 3 * n);      /* (each written once, in order: the store is write-combined) */
    if (nrm) memcpy(nrm, p->nrm, sizeof(float) * 3 * n);
    if (uv && rs) {                                  /* a region's tile: its scale in the coordinates (rnd: the
                                                      * texture matrix did this, on the CPU in gl4es, every draw) */
        for (size_t k = 0; k < n; k++) {
            uv[2 * k] = p->uv[2 * k] * rs[0];
            uv[2 * k + 1] = p->uv[2 * k + 1] * rs[1];
        }
    } else if (uv)
        memcpy(uv, p->uv, sizeof(float) * 2 * n);
    if (col) memcpy(col, p->col, sizeof(float) * 4 * n);
    for (size_t k = 0; k < ni; k++)
        idx[k] = (uint16_t)p->tris[k];
    uint32_t rev = pb->rev, nverts = pb->nverts, ntris = pb->ntris;
    const float *ppos = pb->pos, *pnrm = pb->nrm, *puv = pb->uv, *pcol = pb->col;
    const uint32_t *ptris = pb->tris;
    uint8_t churn = pb->churn;
    struct SkinGpu *sg = pb->sg;
    pb->sg = NULL;
    xbox_static_unregister(pb);                      /* the copies and indices made before */
    pb->sg = sg;
    pb->rev = rev; pb->nverts = nverts; pb->ntris = ntris;
    pb->pos = ppos; pb->nrm = pnrm; pb->uv = puv; pb->col = pcol; pb->tris = ptris;
    pb->churn = churn;
    pb->gpos = pos; pb->gnrm = nrm; pb->guv = uv; pb->gcol = col;
    pb->idx16 = idx;
    pb->nidx16 = (uint32_t)ni;
    pb->vbo = 1;
    pb->own = 1;
    pb->stale = 0;
    pb->baked = uv && rs;
    pb->rs[0] = rs ? rs[0] : 1;
    pb->rs[1] = rs ? rs[1] : 1;
    return 1;
}
#endif

#ifdef RAMPAGE_XBOX
static void bat_drop(Renderer *r);
static void rig_drop(Renderer *r);
#endif

static void drop_buffers(Renderer *r)
{
#ifdef RAMPAGE_XBOX
    bat_drop(r);
    rig_drop(r);
#endif
    for (uint32_t m = 0; m < r->nmbuf; m++) {
        MeshBuf *mb = &r->mbuf[m];
        for (uint32_t k = 0; mb->pb && k < mb->nparts; k++) {
#ifdef RAMPAGE_XBOX
            xbox_static_unregister(&mb->pb[k]);
#else
            GLuint id[2] = { mb->pb[k].vbo, mb->pb[k].ibo };
            if (id[0] || id[1])
                g_buf.del(2, id);
#endif
        }
        free(mb->pb);
    }
    free(r->mbuf);
    r->mbuf = NULL;
    r->nmbuf = 0;
}

/* part i of scene mesh sm in buffers, made at its first draw and again when its arrays changed (a new block's meshes
 * reuse the indices; the buildings' facades change as they are damaged: MeshPart.rev); NULL: not a scene mesh (the
 * effects' shapes, the shell's), no buffers in this GL, or out of memory */
static const PartBuf *part_buffers(Renderer *r, const SceneMesh *sm, uint32_t i)
{
    const Scene *s = r->scene;
    if (!s || !s->meshes || (uintptr_t)sm < (uintptr_t)s->meshes ||
        (uintptr_t)sm >= (uintptr_t)(s->meshes + s->nmeshes) || !buffers_ok())
        return NULL;
    uint32_t mi = (uint32_t)(sm - s->meshes);
    if (r->nmbuf < s->nmeshes) {
        MeshBuf *b = realloc(r->mbuf, sizeof *b * s->nmeshes);
        if (!b)
            return NULL;
        memset(b + r->nmbuf, 0, sizeof *b * (s->nmeshes - r->nmbuf));
        r->mbuf = b;
        r->nmbuf = s->nmeshes;
    }
    const Mesh *m = &sm->mesh;
    MeshBuf *mb = &r->mbuf[mi];
    if (mb->parts != m->parts || mb->nparts != m->nparts) {
        for (uint32_t k = 0; mb->pb && k < mb->nparts; k++) {
            GLuint id[2] = { mb->pb[k].vbo, mb->pb[k].ibo };
            if (id[0] || id[1])
                g_buf.del(2, id);
        }
        free(mb->pb);
        mb->pb = calloc(m->nparts ? m->nparts : 1, sizeof *mb->pb);
        mb->parts = m->parts;
        mb->nparts = mb->pb ? m->nparts : 0;
        if (!mb->pb)
            return NULL;
    }
    const MeshPart *p = &m->parts[i];
    PartBuf *pb = &mb->pb[i];
    if (!pb->vbo || pb->rev != p->rev || pb->nverts != p->nverts || pb->ntris != p->ntris || pb->pos != p->pos ||
        pb->nrm != p->nrm || pb->uv != p->uv || pb->col != p->col || pb->tris != p->tris) {
        size_t n = p->nverts, floats = 3 * n + (p->nrm ? 3 * n : 0) + (p->uv ? 2 * n : 0) + (p->col ? 4 * n : 0);
        if (floats > r->nstage) {
            float *st = realloc(r->stage, sizeof *st * floats);
            if (!st)
                return NULL;
            r->stage = st;
            r->nstage = floats;
        }
        float *o = r->stage;
        memcpy(o, p->pos, sizeof *o * 3 * n);
        o += 3 * n;
        pb->at_nrm = p->nrm ? sizeof *o * (size_t)(o - r->stage) : 0;
        if (p->nrm) { memcpy(o, p->nrm, sizeof *o * 3 * n); o += 3 * n; }
        pb->at_uv = p->uv ? sizeof *o * (size_t)(o - r->stage) : 0;
        if (p->uv) { memcpy(o, p->uv, sizeof *o * 2 * n); o += 2 * n; }
        pb->at_col = p->col ? sizeof *o * (size_t)(o - r->stage) : 0;
        if (p->col) { memcpy(o, p->col, sizeof *o * 4 * n); o += 4 * n; }
        GLenum use = pb->vbo ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;   /* made again: it changes */
        if (!pb->vbo) {
            GLuint id[2] = { 0, 0 };
            g_buf.gen(2, id);
            if (!id[0] || !id[1]) {
                if (id[0] || id[1])
                    g_buf.del(2, id);
                return NULL;            /* client arrays this draw; never publish an incomplete pair */
            }
            pb->vbo = id[0];
            pb->ibo = id[1];
        }
        g_buf.bind(GL_ARRAY_BUFFER, pb->vbo);
        g_buf.data(GL_ARRAY_BUFFER, (ptrdiff_t)(sizeof *o * floats), r->stage, use);
        g_buf.bind(GL_ELEMENT_ARRAY_BUFFER, pb->ibo);
        g_buf.data(GL_ELEMENT_ARRAY_BUFFER, (ptrdiff_t)(sizeof *p->tris * 3 * p->ntris), p->tris, use);
        pb->rev = p->rev; pb->nverts = p->nverts; pb->ntris = p->ntris;
        pb->pos = p->pos; pb->nrm = p->nrm; pb->uv = p->uv; pb->col = p->col; pb->tris = p->tris;
    }
    return pb;
}

#ifdef RAMPAGE_XBOX
/* part i of scene mesh sm: its 16-bit indices and, unskinned, its arrays registered as static, made at its first
 * draw and again when the part changed (as part_buffers); NULL: not a scene mesh, the draw is as before */
static const PartBuf *xbox_part(Renderer *r, const SceneMesh *sm, uint32_t i, int skinned, const float *rs)
{
    const Scene *s = r->scene;
    if (!s || !s->meshes || (uintptr_t)sm < (uintptr_t)s->meshes ||
        (uintptr_t)sm >= (uintptr_t)(s->meshes + s->nmeshes))
        return NULL;
    uint32_t mi = (uint32_t)(sm - s->meshes);
    if (r->nmbuf < s->nmeshes) {
        MeshBuf *b = realloc(r->mbuf, sizeof *b * s->nmeshes);
        if (!b)
            return NULL;
        memset(b + r->nmbuf, 0, sizeof *b * (s->nmeshes - r->nmbuf));
        r->mbuf = b;
        r->nmbuf = s->nmeshes;
    }
    const Mesh *m = &sm->mesh;
    MeshBuf *mb = &r->mbuf[mi];
    if (mb->parts != m->parts || mb->nparts != m->nparts) {
        for (uint32_t k = 0; mb->pb && k < mb->nparts; k++)
            xbox_static_unregister(&mb->pb[k]);
        free(mb->pb);
        mb->pb = calloc(m->nparts ? m->nparts : 1, sizeof *mb->pb);
        mb->parts = m->parts;
        mb->nparts = mb->pb ? m->nparts : 0;
        if (!mb->pb)
            return NULL;
    }
    const MeshPart *p = &m->parts[i];
    PartBuf *pb = &mb->pb[i];
    int changed = !pb->vbo || pb->rev != p->rev || pb->nverts != p->nverts || pb->ntris != p->ntris ||
                  pb->pos != p->pos || pb->nrm != p->nrm || pb->uv != p->uv || pb->col != p->col ||
                  pb->tris != p->tris || pb->skinned != (uint8_t)(skinned != 0);
    if (changed && !skinned) {
        /* An unposed part keeps copies of its own in GPU memory (xbox_own_build). When it changes (a facade's cell
         * is damaged) the copies it has go on being drawn, with their own indices, until this frame's budget lets
         * them be made again: no draw of a changed part goes back to the staging arena and gl4es, which was the
         * slowest path and the one a city under attack was drawn through. */
        int had = pb->vbo == 1 && pb->own;
        if (!had)
            xbox_static_unregister(pb);              /* (posed before, or staged: nothing to keep) */
        pb->rev = p->rev; pb->nverts = p->nverts; pb->ntris = p->ntris;
        pb->pos = p->pos; pb->nrm = p->nrm; pb->uv = p->uv; pb->col = p->col; pb->tris = p->tris;
        pb->skinned = 0;
        if (!pb->stale)
            pb->stale_since = r->xframe;
        pb->stale = 1;
        if (!had)
            pb->vbo = 2;                             /* staged until its copies are made (below, at once) */
    } else if (changed) {
        if (pb->vbo == 1 && pb->churn < 255)
            pb->churn++;                             /* (a posed part's own arrays changing: registering them
                                                      * again and again costs more than staging them) */
        xbox_static_unregister(pb);
        size_t n = p->nverts;
        pb->rev = p->rev; pb->nverts = p->nverts; pb->ntris = p->ntris;
        pb->pos = p->pos; pb->nrm = p->nrm; pb->uv = p->uv; pb->col = p->col; pb->tris = p->tris;
        if (n <= 65536 && p->ntris && p->tris && (pb->idx16 = malloc(sizeof *pb->idx16 * 3 * (size_t)p->ntris))) {
            for (size_t k = 0; k < 3 * (size_t)p->ntris; k++)
                pb->idx16[k] = (uint16_t)p->tris[k];
            pb->nidx16 = 3 * p->ntris;
        }
        pb->vbo = pb->churn <= 3 && n && p->pos && (!skinned || p->uv || p->col) ? 3 : 2;
        pb->wait = 0;
        pb->skinned = (uint8_t)(skinned != 0);
    }
    if (pb->vbo == 1 && pb->own && !pb->stale && pb->baked != (uint8_t)(rs != NULL && p->uv != NULL))
        pb->stale = 1;                               /* (its material's region came or went) */
    if (pb->stale) {
        /* The prototype rewrites one building a frame and lets a wall that was never built exceed that. Here: a part
         * with no copies yet is copied at once (it cannot be drawn from old ones); a changed one when the frame has
         * budget left, and one a frame whatever its size. */
        size_t n = p->nverts;
        size_t bytes = n * (12 + (p->nrm ? 12 : 0) + (p->uv ? 8 : 0) + (p->col ? 16 : 0));
        int had = pb->vbo == 1 && pb->own;
        /* (a part that changes every frame, a falling building's, would take the frame's budget each time and
         * leave the ones drawn after it old for good: one that has waited five frames, the prototype's limit for
         * its rationed building jobs, draws on a budget of its own) */
        int starved = had && r->own_budget < bytes && r->xframe - pb->stale_since >= 5 &&
                      (r->own_starved >= bytes || r->own_starved == OWN_STARVED_BYTES);   /* (the first of them
                                                      * whatever its size: a large one is not left old for good) */
        if (!had || !r->own_made || r->own_budget >= bytes || starved) {
            if (g_xbox_own_parts && xbox_own_build(pb, p, rs)) {
                if (had) {
                    r->own_made++;
                    if (starved)
                        r->own_starved = r->own_starved > bytes ? r->own_starved - (uint32_t)bytes : 0;
                    else
                        r->own_budget = r->own_budget > bytes ? r->own_budget - (uint32_t)bytes : 0;
                }
            } else {                                 /* no room in the store: staged from the part's arrays, with
                                                      * 16-bit indices as before (not tried again until it changes) */
                xbox_static_unregister(pb);
                pb->vbo = 2;
                if (n <= 65536 && p->ntris && p->tris &&
                    (pb->idx16 = malloc(sizeof *pb->idx16 * 3 * (size_t)p->ntris))) {
                    for (size_t k = 0; k < 3 * (size_t)p->ntris; k++)
                        pb->idx16[k] = (uint16_t)p->tris[k];
                    pb->nidx16 = 3 * p->ntris;
                }
            }
        }
    }
    /* (A posed part.) Registered only once it has been drawn on its own for a second and a half: a part on its way
     * into a batch (its item settling) would leave a second copy of itself in the store, and the store is small. */
    if (pb->vbo == 3 && !(pb->sg && !pb->sg->failed) && ++pb->wait >= 90) {   /* (not one the GPU poses: its
                                                                                 * groups have their own copies) */
        size_t n = p->nverts;
        /* (a posed part: its positions and normals are the skin's, only what it keeps of its own is registered) */
        int ok = (pb->skinned || gliStaticArrayAdd(p->pos, sizeof(float) * 3 * n, pb)) &&
                 (pb->skinned || !p->nrm || gliStaticArrayAdd(p->nrm, sizeof(float) * 3 * n, pb)) &&
                 (!p->uv || gliStaticArrayAdd(p->uv, sizeof(float) * 2 * n, pb)) &&
                 (!p->col || gliStaticArrayAdd(p->col, sizeof(float) * 4 * n, pb));
        if (!ok) {
            uint16_t *keep = pb->idx16;
            pb->idx16 = NULL;
            pb->vbo = 1;                             /* (so the ones that were added are removed) */
            xbox_static_unregister(pb);
            pb->idx16 = keep;
            pb->vbo = 2;
        } else {
            pb->vbo = 1;
            pb->gpos = pb->skinned ? NULL : gliStaticArrayAddress(p->pos);
            pb->gnrm = pb->skinned || !p->nrm ? NULL : gliStaticArrayAddress(p->nrm);
            pb->guv = p->uv ? gliStaticArrayAddress(p->uv) : NULL;
            pb->gcol = p->col ? gliStaticArrayAddress(p->col) : NULL;
        }
    }
    return pb;
}
#endif

void rnd_scene(Renderer *r, const Scene *s)
{
    drop_textures(r);
    drop_buffers(r);
    /* A new scene can reuse the same mesh storage; keep capacity, not its bounds. */
    if (r->bound)
        memset(r->bound, 0, sizeof *r->bound * r->nbound);
    r->scene = s;
}

void rnd_free(Renderer *r)
{
    if (!r)
        return;
    drop_textures(r);
    drop_buffers(r);
    if (r->video_tex)
        glDeleteTextures(1, &r->video_tex);
    free(r->held);
    free(r->bound);
    free(r->stage);
    free(r->water_uv);
#ifdef RAMPAGE_XBOX
    free(r->pal);
    free(r->bat_idx);
#endif
    free(r);
}

void rnd_video(Renderer *r, const uint8_t *rgba, int w, int h)
{
    r->video = rgba;
    r->video_w = w;
    r->video_h = h;
    r->video_new = 1;
#ifdef RAMPAGE_XBOX
    /* Release the movie's GPU allocation before loading the shell/world. Flush
     * deferred GL draws and wait for DMA before the backend frees this storage. */
    if (!rgba && r->video_tex) {
        glFinish();
        glDeleteTextures(1, &r->video_tex);
        r->video_tex = 0;
        r->video_tex_w = r->video_tex_h = 0;
    }
#endif
}

/* the movie's texture, uploaded when it changed */
#ifdef RAMPAGE_XBOX
/* The state a run of draw_part calls leaves behind, so that a part only sends what differs from the part before:
 * every GL call crosses gl4es and nxdk-gles11, and a part repeating the dozen settings of the one before it was a
 * third of its cost on the console. -1 / not known: unknown, the next use sets it. Anything else that draws or
 * changes this state ends the run first (xs_end): the tracker then knows nothing, and the lighting, switched lazily
 * for the prelit parts, is put back as the run found it. */
static struct {
    int cull, tex2d, nrm, uv, col, light, light_base, tex_known;
    GLuint tex, wrap_id[64];
    GLint wrap[64];
} xs = { -1, -1, -1, -1, -1, -1, -1, 0, 0, { 0 }, { 0 } };

static void xs_end(void)
{
    if (xs.light_base >= 0 && xs.light != xs.light_base) {
        if (xs.light_base) glEnable(GL_LIGHTING);
        else glDisable(GL_LIGHTING);
    }
    xs.cull = xs.tex2d = xs.nrm = xs.uv = xs.col = xs.light = xs.light_base = -1;
    xs.tex_known = 0;
    memset(xs.wrap_id, 0, sizeof xs.wrap_id);
}
static void xs_enable(int *st, GLenum cap, int on)
{
    if (*st == on) return;
    *st = on;
    if (on) glEnable(cap);
    else glDisable(cap);
}
static void xs_client(int *st, GLenum array, int on)
{
    if (*st == on) return;
    *st = on;
    if (on) glEnableClientState(array);
    else glDisableClientState(array);
}
static void xs_bind(GLuint id)
{
    xs_enable(&xs.tex2d, GL_TEXTURE_2D, 1);
    if (xs.tex_known && xs.tex == id) return;
    xs.tex = id;
    xs.tex_known = 1;
    glBindTexture(GL_TEXTURE_2D, id);
}
static void xs_wrap(GLuint id, GLint wrap)       /* of the texture xs_bind just bound */
{
    unsigned k = id & 63u;
    if (xs.wrap_id[k] == id && xs.wrap[k] == wrap) return;
    xs.wrap_id[k] = id;
    xs.wrap[k] = wrap;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
}
static int xs_light_base(void)                   /* the lighting as the run found it */
{
    if (xs.light_base < 0)
        xs.light = xs.light_base = glIsEnabled(GL_LIGHTING) ? 1 : 0;
    return xs.light_base;
}
static void xs_light(int on)
{
    xs_light_base();
    xs_enable(&xs.light, GL_LIGHTING, on);
}
#define XS_END() xs_end()
#define XS_TEXTURE_BOUND() (xs.tex_known = 0)    /* something else bound a texture */
#else
#define XS_END() ((void)0)
#define XS_TEXTURE_BOUND() ((void)0)
#endif

static GLuint video_texture(Renderer *r)
{
    if (!r->video)
        return 0;
    if (!r->video_tex) {
        glGenTextures(1, &r->video_tex);
        glBindTexture(GL_TEXTURE_2D, r->video_tex);
        XS_TEXTURE_BOUND();
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
#ifdef RAMPAGE_XBOX
        /* gl4es ES1 limited NPOT requires an explicit single level before upload;
         * otherwise a 640x480 movie gets padded to 1024x512. */
        glTexParameteri(GL_TEXTURE_2D, 0x813D /* GL_TEXTURE_MAX_LEVEL */, 0);
#endif
        r->video_new = 1;
    }
    glBindTexture(GL_TEXTURE_2D, r->video_tex);
    XS_TEXTURE_BOUND();
    if (r->video_new) {
        if (r->video_tex_w != r->video_w || r->video_tex_h != r->video_h) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, r->video_w, r->video_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, r->video);
            r->video_tex_w = r->video_w;
            r->video_tex_h = r->video_h;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, r->video_w, r->video_h, GL_RGBA, GL_UNSIGNED_BYTE, r->video);
        }
    }
    r->video_new = 0;
    return r->video_tex;
}

uint32_t rnd_triangles(const Renderer *r) { return r->drawn; }

#if defined RAMPAGE_XBOX && defined RAMPAGE_XBOX_TEST_ARGS
/* test builds: the draws sent to the GPU and their indices, by kind (0 a batch, 1 a merged moving model, 2 a run of
 * quads, 3 a skin posed by the GPU, 4 a part on the direct path, 5 a part through gl4es), for main.c's line every
 * 5 s: at 1280 x 720 the frame is the GPU's, and it pays for each draw as well as for each triangle. */
static struct { unsigned n[6], idx[6], frames; } xdraws;
#define XDRAW(kind, indices) (xdraws.n[kind]++, xdraws.idx[kind] += (unsigned)(indices))
void rnd_draw_stats(char *out, size_t cap)
{
    static const char *const NAME[6] = { "batch", "rigid", "quads", "skin", "part", "gl4es" };
    float f = xdraws.frames ? 1.0f / xdraws.frames : 0;
    int at = 0;
    for (int k = 0; k < 6 && at >= 0 && (size_t)at < cap; k++)
        at += snprintf(out + at, cap - at, "%s%s=%.0f/%.0f", k ? " " : "", NAME[k], xdraws.n[k] * f,
                       xdraws.idx[k] * f / (k == 2 ? 2.0f : 3.0f));
    memset(&xdraws, 0, sizeof xdraws);
}
void rnd_draw_stats_frame(void) { xdraws.frames++; }
#else
#define XDRAW(kind, indices) ((void)0)
#endif

/* a bundle's texture by name index, 0 none (a material's or a mesh's); texture_at with zero_ok: index 0 is a name too (an
 * effect's texture found by name: MemoryCard.bdp's Mem_arrow is name 0) */
static BundleTex *bundle_tex(Renderer *r, const Bundle *b)
{
    for (int k = 0; k < r->ncache; k++)
        if (r->cache[k].b == b)
            return &r->cache[k];
    if (r->ncache == 16)
        return NULL;
    BundleTex *bt = &r->cache[r->ncache];
    memset(bt, 0, sizeof *bt);
    bt->b = b;
    bt->n = b->nnames;
    bt->tex = calloc(bt->n ? bt->n : 1, sizeof *bt->tex);
    if (!bt->tex)
        return NULL;
    r->ncache++;                         /* publish only a usable table; allocation failure can retry */
    return bt;
}

#if defined(RAMPAGE_XBOX) && defined(RAMPAGE_XBOX_EXACT_PALETTE)
extern GLboolean gliTextureTryPalette(void);
/* Only immutable world-cache uploads opt in. HUD, movies and mipmapped images
 * retain their existing storage. The driver accepts only exact dictionaries
 * whose page-rounded allocation shrinks, preserving RGBA storage on fallback.
 * Flush the frontend before inspecting its bound native texture; the native
 * conversion waits for queued GPU reads before replacing storage. */
static void try_exact_palette(Renderer *r)
{
    if (r->in_hud || g_mips)
        return;
    glFinish();
    gliTextureTryPalette();
    pump_output(r);
}
#else
#define try_exact_palette(r) ((void)0)
#endif

#ifdef RAMPAGE_XBOX
int g_xbox_direct_p8 = 1;                /* --xoff p8: every texture expanded to full colour first, as before */
int g_xbox_p8_mips = 1;                  /* the 8-bit world textures with their smaller levels (--xoff mips: without).
                                          * The studio's Xbox build has full chains; without them the GPU reads a
                                          * whole 256 x 256 texture for a wall a few pixels high */
/* test builds' --xskip N: kinds of drawing left out, to measure on a console what each costs. 1 the sky, 2 the
 * street, 4 the opaque batches, 8 the blended batches, 16 the items of the main pass, 32 the effects, 64 the HUD,
 * 128 the shadows, 256 the water, 512 the main pass's skinned items, 1024 its others, 2048 the parts that go through
 * gl4es. Always 0 in a normal build. */
int g_xbox_skip;
#ifdef RAMPAGE_XBOX_TEST_ARGS
#define XSKIP(bit) (g_xbox_skip & (bit))
#else
#define XSKIP(bit) 0
#endif
/* How much of the picture the world's effects may blend over in one frame before draw_fx thins them, in hundredths
 * of a 1280 x 720 screen (--xfxbudget N; 0: never thinned). Measured on a console at 1280 x 720 (four monsters, Las
 * Vegas, 34 windows of 5 s): a frame takes 27.2 ms + 2.5 ms for each screen the effects cover, and they cover 2 to 9
 * (a single frame up to 28) while buildings come down. 200 keeps such a frame near 32 ms. At 640 x 480 the same
 * number of pixels is six screens. */
int g_xbox_fx_budget = 200;
/* Above 480 lines the budget follows the frame: what the rest of the frame takes is measured (the frame's time less
 * FX_SCREEN_MS for each screen of effects drawn in it, averaged over about a second), and the effects get what is
 * left of g_xbox_fx_frame_ms (--xfxframe N; 0: the fixed budget), between 0.25 and 1.5 of g_xbox_fx_budget. A street
 * that alone takes 31 ms leaves them under a screen, one that takes 26 over two. Not at 480 lines, where the frame's
 * time is the game's own work and thinner smoke would buy nothing. */
int g_xbox_fx_frame_ms = 32;
#define FX_SCREEN_MS 2.5f                /* a screen of blended effects at 1280 x 720 on a console's GPU (measured) */
/* 0: a frame with a late sky (below) is not cleared to the fog's colour first; what the scene leaves bare is filled
 * afterwards (xbox_far_fill). Clearing 1280 x 720 measured 1.2 ms of a console's frame, twice over, and the scene
 * paints all but a strip at the bottom edge. 1 (--xon clearcolor): cleared, as before. 2 (test builds' --xon
 * magenta): cleared to magenta and filled, to see in a capture what neither the scene nor the fill reaches. */
int g_xbox_clear_color;

/* The fog's colour behind everything that is drawn so far: one rectangle just inside the far plane, depth-tested and
 * not written. Where the street and the buildings already are it draws nothing. */
static void xbox_far_fill(const float colour[3], float tx, float ty)
{
    xs_end();
    glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT | GL_LIGHTING_BIT);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);
    glColor4f(colour[0], colour[1], colour[2], 1);
    const float z = -2950.0f, x = 2950.0f * tx * 1.05f, y = 2950.0f * ty * 1.05f;   /* (the far plane is at 3,000) */
    const float v[12] = { -x, -y, z,  x, -y, z,  x, y, z,  -x, y, z };
    glVertexPointer(3, GL_FLOAT, 0, v);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glPopMatrix();
    glPopClientAttrib();
    glPopAttrib();
    XS_TEXTURE_BOUND();
}
int g_xbox_sky_late = 1;                 /* the sky after the street and the batched buildings, behind them by the
                                          * depth test (--xoff skylate: first, over the whole screen, as the PS2) */
#endif
#if defined(RAMPAGE_XBOX) && defined(RAMPAGE_XBOX_EXACT_PALETTE)
extern GLboolean gliTexturePalette8(GLsizei width, GLsizei height, const GLubyte *indices, const GLubyte *palette_rgba,
                                    GLboolean mips);
extern void gliTexturePlaceholderNext(void);

/* The PS2's world textures are 8-bit with a palette, and that is what the Xbox's GPU stores them as. They used to
 * go there the long way round: expanded to full colour, uploaded, then (try_exact_palette) the GPU waited for, the
 * upload read back out of its memory and its 256 colours found again. Here the indices and the palette are handed
 * over as they are (the studio's Xbox build keeps its textures in the GPU's own formats on the disc for the same
 * reason). The same texture results: the same colours at the same texels.
 * Only what try_exact_palette took: the world's textures of power-of-two sizes from 8 texels, without mipmaps.
 * Returns the GL texture, 0 for a texture that has to go the ordinary way. */
static GLuint xbox_texture_p8(Renderer *r, const Bundle *b, uint32_t name)
{
    if (!g_xbox_direct_p8 || r->in_hud || r->in_hud_quad || g_mips)
        return 0;
    uint8_t *index = NULL, pal[256][4];
    int w = 0, h = 0;
    if (tex_indexed(b, bdp_entry_of(b, name, ET_TEXTURE), &index, pal, &w, &h) != TEX_DECODE_OK)
        return 0;
    GLuint id = 0;
    if (w >= 8 && h >= 8 && w <= 1024 && h <= 1024 && !(w & (w - 1)) && !(h & (h - 1)))
        glGenTextures(1, &id);
    if (!id) {
        free(index);
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, id);
    XS_TEXTURE_BOUND();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    /* the image's size and format for gl4es and the driver, no pixels (the driver told to give it one page: its
     * storage is replaced at once); then the storage itself */
    gliTexturePlaceholderNext();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    GLboolean ok = gliTexturePalette8(w, h, index, &pal[0][0], g_xbox_p8_mips ? GL_TRUE : GL_FALSE);
    free(index);
    if (!ok) {                                       /* (no memory, or not the storage expected: the ordinary way) */
        glDeleteTextures(1, &id);
        return 0;
    }
    tex_aniso();
    pump_output(r);
    return id;
}
#endif

#if defined(RAMPAGE_XBOX) && defined(RAMPAGE_XBOX_EXACT_PALETTE)
extern GLboolean gliTextureNative(GLsizei width, GLsizei height, GLint kind, const GLubyte *data, GLsizei size,
                                  GLsizei levels);
int g_xbox_gx_native = 1;                /* gxnative: the Wii's textures in the GPU's own formats (--xoff gxnative:
                                          * expanded to full colour, as before) */
int g_xbox_gx_skip = -1;                 /* --xgxskip N: that many of the largest levels left out of the Wii's
                                          * textures of 512 texels a side or more (a half, a quarter of their
                                          * size). -1: one at more than 480 lines, where the screens take 8 MB
                                          * more of the console's memory, none otherwise. Dallas has four textures
                                          * of 1024 and the Wii's monsters one of 512 each (the PS2's: 256). */
extern void gliTexturePlaceholderNext(void);

/* A Wii bundle's world texture as the GPU stores it (tex.c tex_gx_native: DXT1 for the Wii's CMPR, 16 bits for
 * its RGB5A3, 8 bits with a grey palette for its I8), with the mip levels of its file. Expanded to full colour,
 * as the PC does, Dallas's textures alone are 21 MB of a console's 64. Returns the GL texture, 0 for a texture
 * that has to go the ordinary way (the HUD's, any other format or size). */
static GLuint xbox_texture_gx(Renderer *r, const Bundle *b, uint32_t name)
{
    if (!g_xbox_gx_native || !b->wii || r->in_hud || r->in_hud_quad || g_mips)
        return 0;
    const BdpEntry *e = bdp_entry_of(b, name, ET_TEXTURE);
    uint8_t *data = NULL;
    uint32_t size = 0;
    int kind = 0, w = 0, h = 0, levels = 0;
    int xbox_pixel_height(void);
    int big = e && e->size >= 0x10 && (rd16(e->data + 8) >= 512 || rd16(e->data + 10) >= 512);
    int skip = g_xbox_gx_skip >= 0 ? g_xbox_gx_skip : xbox_pixel_height() > 480 ? 1 : 0;
    if (tex_gx_native(b, e, big ? skip : 0, &kind, &data, &size, &w, &h, &levels) != TEX_DECODE_OK)
        return 0;
    GLuint id = 0;
    glGenTextures(1, &id);
    if (!id) {
        free(data);
        return 0;
    }
    glBindTexture(GL_TEXTURE_2D, id);
    XS_TEXTURE_BOUND();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    /* the image's size and format for gl4es and the driver, no pixels (the driver told to give it one page: its
     * storage is replaced at once); then the storage itself */
    gliTexturePlaceholderNext();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    GLboolean ok;
    if (kind == TEX_NATIVE_I8) {
        static uint8_t grey[256][4];
        if (!grey[255][0])
            for (int i = 0; i < 256; i++)
                grey[i][0] = grey[i][1] = grey[i][2] = grey[i][3] = (uint8_t)i;   /* (GX: intensity into alpha too) */
        ok = gliTexturePalette8(w, h, data, &grey[0][0], g_xbox_p8_mips ? GL_TRUE : GL_FALSE);
    } else
        ok = gliTextureNative(w, h, kind, data, (GLsizei)size, levels);
    free(data);
    if (!ok) {                                       /* (no memory, or not the storage expected: the ordinary way) */
        glDeleteTextures(1, &id);
        return 0;
    }
    tex_aniso();
    pump_output(r);
    return id;
}
#endif

static GLuint texture_at(Renderer *r, const Bundle *b, uint32_t name, int zero_ok)
{
    pump_output(r);
    BundleTex *bt = bundle_tex(r, b);
    if (!bt || (!name && !zero_ok) || name >= bt->n)
        return 0;
    GLuint *slots = bt->tex;
#ifdef RAMPAGE_XBOX
    if (r->in_hud_quad) {
        /* A source image can be shared by 2D and 3D materials. Keep separate GPU
         * objects: the clamped native-NPOT upload cannot later be made repeatable
         * by changing its wrap flag, while the 3D upload may require POT scaling. */
        if (!bt->hud_tex)
            bt->hud_tex = calloc(bt->n ? bt->n : 1, sizeof *bt->hud_tex);
        slots = bt->hud_tex;
    }
#endif
    if (!slots)
        return 0;
    if (!slots[name]) {
        uint8_t *rgba;
        int w, h;
        slots[name] = ~0u;
        XHITCH(textures, 1);
#if defined(RAMPAGE_XBOX) && defined(RAMPAGE_XBOX_EXACT_PALETTE)
        {
            GLuint direct = b->wii ? xbox_texture_gx(r, b, name) : xbox_texture_p8(r, b, name);
            if (direct) {
                slots[name] = direct;
                return direct;
            }
        }
#endif
        int decoded = tex_decode(b, bdp_entry_of(b, name, ET_TEXTURE), &rgba, &w, &h);
        if (decoded == TEX_DECODE_NOMEM) {
            slots[name] = 0;             /* missing/invalid images stay negative-cached; OOM can retry */
            return 0;
        }
#if defined RAMPAGE_XBOX && defined RAMPAGE_XBOX_TEST_ARGS
        if (decoded == TEX_DECODE_OK && b->wii) {    /* test builds: a Wii texture that went the full-colour way */
            const BdpEntry *we = bdp_entry_of(b, name, ET_TEXTURE);
            printf("wii texture full colour: %s %dx%d format %u%s\n", bdp_name(b, name), w, h,
                   we ? (unsigned)rd32(we->data + 4) : 0u, r->in_hud_quad ? " (hud)" : r->in_hud ? " (hud 3d)" : "");
        }
#endif
        if (decoded == TEX_DECODE_OK) {
            GLuint id = 0;
            glGenTextures(1, &id);
            if (!id) {
                free(rgba);
                slots[name] = 0;         /* no GL object: retry rather than cache a failed upload */
                return 0;
            }
            glBindTexture(GL_TEXTURE_2D, id);
            XS_TEXTURE_BOUND();
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
#ifdef RAMPAGE_XBOX
            if (r->in_hud_quad) {
                /* Set the 2D HUD's real sampling mode before upload so gl4es can
                 * keep NPOT screens at their native dimensions, not POT copies.
                 * Do not do this for GUI 3D layers, which can tile textures. */
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, 0x813D /* GL_TEXTURE_MAX_LEVEL */, 0);
            }
#endif
            /* port extra: a texture pack's PNG (texpack.c), named after the decoded mip 0; hashed only with a pack
             * or --dumptex */
            uint64_t hash = tp_active() ? tp_hash('T', rgba, (uint32_t)w * (uint32_t)h * 4, w, h) : 0;
            if (!hash || !tp_replace(hash, TP_ASIS)) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
                if (g_mips && !r->in_hud)            /* port extra: mips= (the world's; the HUD's as before) */
                    upload_mips(rgba, w, h);
            }
            if (!r->in_hud)
                tex_aniso();                         /* port extra: aniso= */
            try_exact_palette(r);
            if (hash) {
                tp_scope(b->scope);
                tp_dump(hash, rgba, w, h);
            }
            free(rgba);
            slots[name] = id;
        }
    }
    return slots[name] == ~0u ? 0 : slots[name];
}
static GLuint texture(Renderer *r, const Bundle *b, uint32_t name) { return texture_at(r, b, name, 0); }

/* ---- a material's texture region (MAT_REGION, docs/FORMAT_BDP.md "Material flags") [R]. 0x25F960 makes it the
 * material's CLAMP_1 (+0x80): REGION_REPEAT on S and T (WMS = WMT = 3), MINU / MINV the tile's size - 1 and MAXU / MAXV
 * its start; a pass on texture slot 0 sends it in place of the texture's own CLAMP_1 (0x265840 puts it at 0x4CBED0,
 * 0x2598A0 over the texture's +0x90 in the packet; a pass on slot 1 sends none). The GS reads texel u at
 * (u & MINU) | MAXU: the part's UVs, in units of the whole texture, wrap within the tile. Here the tile is cut into a
 * texture of its own with GL_REPEAT and the texture matrix scales the UVs by the texture's size over the tile's: the
 * same texels at the same UVs, the bilinear taps wrapped in the tile as the GS's are. */

/* the tile in texels (x, y, w, h) of a tw x th texture, as 0x25FC08 .. 0x25FE50 builds the register: the size
 * round(16 x width) (+ 0.5, cut as the EE's cvt.w.s) down to a power of two (0x25FED0 / 0x25FE80 halved, 0x10E0E0 its
 * highest bit), times tw / 16; the start round(u0 x tw) + size / 2 cut to a multiple of the size; both 10 bits.
 * 0 when that is the whole texture or no tile: a size 0 masks u with 0x3FF and starts at 0, the plain repeat of a
 * texture up to 1024 wide [the start past the texture's end: never in the data, kept inside it] */
static int region_rect(const float reg[4], int tw, int th, int rect[4])
{
    const int size[2] = { tw, th };
    for (int k = 0; k < 2; k++) {
        int s = (int)(16 * reg[2 + k] + 0.5f);
        while (s & (s - 1))
            s &= s - 1;
        int len = size[k] * s / 16;
        if (len <= 0 || len > 0x400 || len > size[k])
            return 0;
        int start = (((int)(reg[k] * size[k] + 0.5f) + len / 2) & ~(len - 1)) & 0x3FF;
        if (start + len > size[k])
            start = size[k] - len;
        rect[k] = start;
        rect[2 + k] = len;
    }
    return rect[2] < tw || rect[3] < th;
}

/* texture `name` cut to the region reg (region_rect), 0 = draw the whole texture; scale the texture matrix's. A
 * texture pack's PNG replaces the whole texture, named after the whole one's hash as texture() names it, and the
 * tile is cut from it at its own size */
static GLuint region_texture(Renderer *r, const Bundle *b, uint32_t name, const float reg[4], float scale[2])
{
    pump_output(r);
    BundleTex *bt = bundle_tex(r, b);
    scale[0] = scale[1] = 1;
    if (!bt || !name || name >= bt->n)
        return 0;
    RegionTex *rt = NULL;
    for (int k = 0; k < bt->nreg && !rt; k++)
        if (bt->reg[k].name == name && !memcmp(bt->reg[k].reg, reg, sizeof bt->reg[k].reg))
            rt = &bt->reg[k];
    if (!rt) {
        RegionTex *grown = realloc(bt->reg, sizeof *grown * (size_t)(bt->nreg + 1));
        if (!grown)
            return 0;
        bt->reg = grown;
        rt = &bt->reg[bt->nreg++];
        memset(rt, 0, sizeof *rt);
        rt->name = name;
        memcpy(rt->reg, reg, sizeof rt->reg);
        rt->scale[0] = rt->scale[1] = 1;
        uint8_t *rgba;
        int w, h, rc[4];
        int decoded = tex_decode(b, bdp_entry_of(b, name, ET_TEXTURE), &rgba, &w, &h);
        if (decoded != TEX_DECODE_OK) {
            if (decoded == TEX_DECODE_NOMEM)
                bt->nreg--;             /* do not negative-cache a transient decoder allocation failure */
            return 0;
        }
        if (region_rect(reg, w, h, rc)) {
            uint64_t hash = tp_active() ? tp_hash('T', rgba, (uint32_t)w * (uint32_t)h * 4, w, h) : 0;
            int pw = w, ph = h;
            uint8_t *pack = hash ? tp_pixels(hash, TP_ASIS, &pw, &ph) : NULL;
            const uint8_t *src = pack ? pack : rgba;
            int x0 = rc[0] * pw / w, y0 = rc[1] * ph / h, cw = rc[2] * pw / w, ch = rc[3] * ph / h;
            cw = cw < 1 ? 1 : cw;
            ch = ch < 1 ? 1 : ch;
            uint8_t *cut = malloc((size_t)cw * ch * 4);
            if (cut) {
                for (int y = 0; y < ch; y++)
                    memcpy(cut + (size_t)y * cw * 4, src + ((size_t)(y0 + y) * pw + x0) * 4, (size_t)cw * 4);
                GLuint id = 0;
                glGenTextures(1, &id);
                if (!id) {
                    free(cut);
                    free(pack);
                    free(rgba);
                    bt->nreg--;         /* keep earlier entries, retry this tile on a later draw */
                    return 0;
                }
                glBindTexture(GL_TEXTURE_2D, id);
            XS_TEXTURE_BOUND();
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                if (pack)
                    tp_upload(cut, cw, ch, TP_ASIS);
                else {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cw, ch, 0, GL_RGBA, GL_UNSIGNED_BYTE, cut);
                    if (g_mips && !r->in_hud)
                        upload_mips(cut, cw, ch);
                }
                if (!r->in_hud)
                    tex_aniso();
                try_exact_palette(r);
                rt->id = id;
                rt->scale[0] = (float)w / rc[2];
                rt->scale[1] = (float)h / rc[3];
                free(cut);
            } else {
                free(pack);
                free(rgba);
                bt->nreg--;             /* scratch OOM is not a permanent whole-texture fallback */
                return 0;
            }
            if (hash) {                              /* --dumptex: the whole texture, as texture() writes it */
                tp_scope(b->scope);
                tp_dump(hash, rgba, w, h);
            }
            free(pack);
        }
        free(rgba);
    }
    scale[0] = rt->scale[0];
    scale[1] = rt->scale[1];
    return rt->id;
}

/* the texture a material shows at time t: itself, or an .ifl's frame then */
static uint32_t shown_texture(Renderer *r, const Bundle *b, uint32_t name, double t)
{
    const BdpEntry *e = bdp_entry(b, name);
    if (!name || !e || e->type != ET_RAW || bdp_entry_of(b, name, ET_TEXTURE))
        return name;
#ifdef RAMPAGE_XBOX
    /* (asked for every part of every frame: the ending of the name is looked at once) */
    BundleTex *bt = bundle_tex(r, b);
    if (bt && name < bt->n && !bt->not_ifl)
        bt->not_ifl = calloc(bt->n ? bt->n : 1, 1);
    if (bt && name < bt->n && bt->not_ifl && bt->not_ifl[name])
        return name;
#endif
    const char *nm = bdp_name(b, name);
    size_t len = strlen(nm);
    if (len < 4 || _stricmp(nm + len - 4, ".ifl") != 0) {
#ifdef RAMPAGE_XBOX
        if (bt && name < bt->n && bt->not_ifl)
            bt->not_ifl[name] = 1;
#endif
        return name;
    }
    Ifl *f = NULL;
    for (int k = 0; k < r->nifl && !f; k++)
        if (r->ifl[k].b == b && r->ifl[k].name == name)
            f = &r->ifl[k];
    if (!f) {
        if (r->nifl == 32)
            return name;
        f = &r->ifl[r->nifl++];
        memset(f, 0, sizeof *f);
        f->b = b;
        f->name = name;
        const char *p = (const char *)e->data, *end = p + e->size;
        float total = 0;
        while (p < end && f->n < IFL_FRAMES) {
            char img[64], line[128];
            size_t k = 0;
            while (p < end && *p != '\n' && k + 1 < sizeof line) line[k++] = *p++;
            while (p < end && (*p == '\n' || *p == '\r')) p++;
            line[k] = 0;
            int frames = 1;
            if (sscanf(line, "%63s %d", img, &frames) < 1)
                continue;
            char *dot = strrchr(img, '.');
            if (dot) *dot = 0;
            int idx = bdp_find_name(b, img);
            if (idx <= 0)
                continue;
            total += (frames > 0 ? frames : 1) / 30.0f;
            f->frame[f->n] = (uint32_t)idx;
            f->until[f->n++] = total;
        }
    }
    if (!f->n)
        return name;
    double at = fmod(t, f->until[f->n - 1]);
    int k = 0;
    while (k + 1 < f->n && at >= f->until[k]) k++;
    return f->frame[k];
}

static void perspective(float fov_deg, float aspect, float zn, float zf)
{
    float f = 1.0f / tanf(fov_deg * 3.14159265f / 360.0f);
    float m[16] = { f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (zf + zn) / (zn - zf), -1, 0, 0, 2 * zf * zn / (zn - zf), 0 };
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(m);
}

static void look(const Camera *c)
{
    float cp = cosf(c->pitch), f[3] = { cp * cosf(c->yaw), cp * sinf(c->yaw), sinf(c->pitch) };
    float s[3] = { f[1], -f[0], 0 };                       /* f x up(0, 0, 1) */
    float sl = sqrtf(s[0] * s[0] + s[1] * s[1]);
    s[0] /= sl; s[1] /= sl;
    float u[3] = { s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0] };
    if (c->use_rows)
        for (int k = 0; k < 3; k++) { s[k] = c->rows[0][k]; f[k] = c->rows[1][k]; u[k] = c->rows[2][k]; }
    float m[16] = { s[0], u[0], -f[0], 0, s[1], u[1], -f[1], 0, s[2], u[2], -f[2], 0, 0, 0, 0, 1 };
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(m);
    glTranslatef(-c->pos[0], -c->pos[1], -c->pos[2]);
}

/* a part drawn over what is behind it by its alpha (draw_part): a blended one, or a see-through material colour */
static int part_blended(const SceneMesh *sm, const MeshPart *p)
{
    float mc[4];
    material_color(sm->b, p->material, mc);
    return p->blend == 1 || (!p->blend && mc[3] < 0.98f);   /* blend 2 writes its depth: drawn in its turn */
}

#ifdef RAMPAGE_XBOX
int xbox_pixel_width(void);
int xbox_pixel_height(void);
#endif
static void draw_part(Renderer *r, const SceneMesh *sm, const Skin *skin, uint32_t i, uint32_t first, double t);

/* the world's cube-map materials [R]: RWorldMaterialFilter (vtable 0x44AAC0) gives a material named BUILDING_CUBEMAP
 * shader 0x1B in place of its own (+0x8, 0x2C2680) and lists it and every RCUBEMAP* one (strncasecmp, 0x2C26E0) for
 * the renderer, which sets the block's EnvMap into the first's texture slot 0 and the others' slot 1 (0x2B92F0 ..
 * 0x2B96F8; cleared by +0x24, 0x2B9150). Only the city bundle's materials go through the filter. 1 BUILDING_CUBEMAP,
 * 2 RCUBEMAP* (in the data: the NY blimp's rcubemap_blimp, kind 5), 0 neither */
static int cube_material(const Scene *s, const SceneMesh *sm, uint32_t material)
{
    if (sm->b != s->city || !s->env_map)
        return 0;
    const char *name = bdp_name(sm->b, material);
    if (!name)
        return 0;
    if (_stricmp(name, "BUILDING_CUBEMAP") == 0)
        return 1;
    return _strnicmp(name, "RCUBEMAP", 8) == 0 ? 2 : 0;
}

/* the sphere-map coordinates of shader 0x1A's second pass and of 0x1B (VU1 programs 0x34F1C8, 0x34FB78) [R]: the view
 * ray V (the vertex in view space: 0x265500 uploads world x view, flag 8 of the pass) reflected on the normal N
 * (rotated by the same matrix, not renormalised), R = (V - 2 (N.V) N) / |V|, then u = 0.5 + 0.3394 Rx / sqrt(1 - Rz),
 * v = 0.5 - 0.3394 Ry / sqrt(1 - Rz), z running into the screen. That is the GL sphere map (whose eye space has z out
 * of the screen: 1 / (2 sqrt(2 (1 + Rz)))) scaled by 0.3394 x 2 sqrt 2 = 0.96 about the centre and turned upside
 * down: the reflection back at the eye at the centre, straight up at the top, all of it within 0.48 of the centre
 * (the env pictures are mirror balls of radius 0.48: the black outside is never reached). View x is taken as right
 * [G: a left-handed view space; the pictures are near symmetric]. On: texture unit 0's coordinates generated */
static void sphere_coords(int on)
{
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    if (on) {
        glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
        glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
        glEnable(GL_TEXTURE_GEN_S);
        glEnable(GL_TEXTURE_GEN_T);
        const float k = 0.3394f * 2.828427f;
        glTranslatef(0.5f, 0.5f, 0);
        glScalef(k, -k, 1);
        glTranslatef(-0.5f, -0.5f, 0);
    } else {
        glDisable(GL_TEXTURE_GEN_S);
        glDisable(GL_TEXTURE_GEN_T);
    }
    glMatrixMode(GL_MODELVIEW);
}

/* port extra: is the item's mesh in the view? Its sphere (its vertices' box, made once per mesh) through its world and
 * the view (mv, the camera's modelview) against the frustum (tx, ty: the half-angles' tangents, near 0.5, far 3000).
 * A skinned item (a monster, a person, a flag's cloth) moves its vertices every frame: always drawn. About a third of
 * a block's items are behind or beside the camera */
static int item_seen(Renderer *r, const DrawItem *it, const float mv[16], float tx, float ty)
{
    const Scene *s = r->scene;
#ifdef RAMPAGE_XBOX
    /* (the GPU poses the people and the army as it draws them: the ones well out of the view are left out, by the
     * bind pose's bound four times over, which a walk or a fall stays inside; a monster and a cloth always drawn) */
    int loose = 0;
    if (it->skin) {
        if (it->skin->cloth || it->skin->gpu_skin <= 0 || scene_monster_slot(s, it->instance) >= 0)
            return 1;
        loose = 1;
    }
#else
    if (it->skin)
        return 1;
#endif
    if (r->nbound < s->nmeshes) {
        MeshBound *b = realloc(r->bound, sizeof *b * s->nmeshes);
        if (!b)
            return 1;
        memset(b + r->nbound, 0, sizeof *b * (s->nmeshes - r->nbound));
        r->bound = b;
        r->nbound = s->nmeshes;
    }
    const Mesh *m = &s->meshes[it->mesh].mesh;
    MeshBound *mb = &r->bound[it->mesh];
    if (mb->parts != m->parts || mb->nparts != m->nparts) {   /* (a new block's meshes reuse the indices) */
        float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
        for (uint32_t k = 0; k < m->nparts; k++)
            for (uint32_t q = 0; q < m->parts[k].nverts; q++)
                for (int c = 0; c < 3; c++) {
                    float x = m->parts[k].pos[3 * q + c];
                    lo[c] = x < lo[c] ? x : lo[c];
                    hi[c] = x > hi[c] ? x : hi[c];
                }
        float r2 = 0;
        for (int c = 0; c < 3; c++) {
            mb->c[c] = lo[c] <= hi[c] ? (lo[c] + hi[c]) / 2 : 0;
            r2 += lo[c] <= hi[c] ? (hi[c] - lo[c]) * (hi[c] - lo[c]) / 4 : 0;
        }
        mb->r = sqrtf(r2);
        mb->parts = m->parts;
        mb->nparts = m->nparts;
    }
    const float *w = it->world.m, *c = mb->c;
    float sc = 0;                                    /* its largest scale */
    for (int k = 0; k < 3; k++) {
        float l = w[4 * k] * w[4 * k] + w[4 * k + 1] * w[4 * k + 1] + w[4 * k + 2] * w[4 * k + 2];
        sc = l > sc ? l : sc;
    }
    float rad = mb->r * sqrtf(sc), wp[3], vp[3];
#ifdef RAMPAGE_XBOX
    if (loose)
        rad = 4 * rad + 2;
#endif
    for (int k = 0; k < 3; k++)
        wp[k] = w[k] * c[0] + w[4 + k] * c[1] + w[8 + k] * c[2] + w[12 + k];
    for (int k = 0; k < 3; k++)
        vp[k] = mv[k] * wp[0] + mv[4 + k] * wp[1] + mv[8 + k] * wp[2] + mv[12 + k];
    float d = -vp[2];                                /* its depth in front of the eye */
    return d + rad >= 0.5f && d - rad <= 3000 && fabsf(vp[0]) <= d * tx + rad * sqrtf(1 + tx * tx) &&
           fabsf(vp[1]) <= d * ty + rad * sqrtf(1 + ty * ty);
}

/* the mesh's parts in order (its lead part first); while r->hold (the ambient occlusion is on: rnd_frame) the
 * blended ones are held back and drawn after it, so it does not darken what shows through them */
static void draw_mesh(Renderer *r, const SceneMesh *sm, const Skin *skin, double t)
{
    const Mesh *m = &sm->mesh;
    uint32_t lead = m->lead < m->nparts ? m->lead : 0, lead_first = 0;
    for (uint32_t i = 0; i < lead; i++)
        lead_first += m->parts[i].nverts;
    for (uint32_t k = 0, first = 0; k <= m->nparts; k++) {
        uint32_t i = k == 0 ? lead : k - 1;              /* the lead, then the others in order */
        if (k > 0 && i == lead) {
            first += m->parts[i].nverts;
            continue;
        }
        uint32_t at = k == 0 ? lead_first : first;       /* the part's first vertex in the skin buffers */
        if (k > 0)
            first += m->parts[i].nverts;
        const MeshPart *p = &m->parts[i];
        if (!p->ntris)
            continue;
#ifdef RAMPAGE_XBOX
        if (r->bat_keys && r->bat_keys[i] >= 0)
            continue;                                /* in a batch (bat_frame) */
#endif
        if (r->hold && part_blended(sm, p)) {
            if (r->nheld == r->cheld) {
                int n = r->cheld ? 2 * r->cheld : 256;
                HeldPart *h = realloc(r->held, sizeof *h * (size_t)n);
                if (!h) {
                    draw_part(r, sm, skin, i, at, t);
                    continue;
                }
                r->held = h;
                r->cheld = n;
            }
            HeldPart *h = &r->held[r->nheld++];
            glGetFloatv(GL_MODELVIEW_MATRIX, h->mv);
            h->sm = sm; h->skin = skin; h->part = i; h->first = at;
            continue;
        }
        draw_part(r, sm, skin, i, at, t);
    }
}

/* the parts draw_mesh held back, in their order, each with its modelview */
#ifdef RAMPAGE_XBOX
/* ---- the standing city in a few large draws ------------------------------------------------------------------
 * A block is some hundreds of small parts (a wall, a trim, a lamp: 44 triangles on average), and on the console a
 * draw costs the same whatever its size: the parts, not their triangles, were the frame. The parts that stand still
 * and are drawn plainly (one texture, opaque, no second layer, the scene's own light) are therefore gathered by what
 * they are drawn with into batches in world space, and a batch is one draw. A batch is built again only when its
 * members change. Everything else is drawn as before, part by part: the skinned, the moving, the blended, the
 * layered, and an item that changed lately: a building being hit (its facade is made again with every blow) for a
 * second, a thing that moved for longer each time it does (a car that stops and starts would otherwise have its
 * batches built again and again). An item drawn with its own ambient (an adornment) is batched with the others of
 * that ambient, the ambient set for the batch. */
/* which of Scene.items this frame's culling kept (rnd_frame); scene_update reads the last frame's */
/* Switches for the console's test builds (main.c --xoff NAME): each takes one of the Xbox drawing paths back to
 * what it replaced, to tell which one a fault on screen belongs to. All on in a normal build. */
int g_xbox_direct_quads = 1;                         /* quads: the effects' and the HUD's straight from GPU memory */
int g_xbox_own_parts = 1;                            /* own: changed and loose parts keep copies in GPU memory */
int g_xbox_region_bake = 1;                          /* region: a region's scale in the texture coordinates */
int g_xbox_gpu_skin = 1;                             /* skin: the GPU poses the skins (city.c) */
int g_xbox_item_skip = 1;                            /* skip: no matrix for an item whose parts are all batched */
int g_xbox_preload = 1;                              /* preload: a level's textures made as it loads */
int g_xbox_rigid = 1;                                /* rigid: a moving model's pieces merged into a few draws */
int g_xbox_sound_preload = 0;                        /* sndpre (--xon): the sample banks whole in memory (main.c) */
uint8_t *g_xbox_item_seen;
uint32_t g_xbox_item_seen_n;

#define BAT_MAX_VERTS 10000u                         /* (its largest array, the colours, is 160 KB of a 256 KB chunk
                                                      * of the static store; smaller batches packed the chunks
                                                      * better but were three times as many draws) */
#define BAT_MAX_INDICES 30000u
#define BAT_SETTLE_FRAMES 60u
int g_xbox_batch_pace = 1;               /* --xoff batpace: a changed part rejoins its batch the frame it is due */

static uint32_t bat_hash(uint32_t h, uint32_t v)
{
    h ^= v;
    return h * 16777619u;
}

/* a batch's arrays are the static store's: built in the memory the NV2A reads, no second copy. When the store has
 * no room there is no batch (its members are drawn part by part): on a 64 MB console the heap has none to spare
 * either. */
static void bat_arrays_free(BatSub *b)
{
    if (b->pos) gliStaticArrayRemove(b->pos, b);
    if (b->nrm) gliStaticArrayRemove(b->nrm, b);
    if (b->uv) gliStaticArrayRemove(b->uv, b);
    if (b->col) gliStaticArrayRemove(b->col, b);
    b->pos = b->nrm = b->uv = b->col = NULL;
    b->registered = 0;
    b->nverts = b->nidx = 0;
}

static void bat_sub_free(BatSub *b)
{
    bat_arrays_free(b);
    free(b->idx); free(b->mem);
    memset(b, 0, sizeof *b);
}

static void bat_drop(Renderer *r)
{
    for (uint32_t g = 0; g < r->nbgroup; g++) {
        for (int k = 0; k < r->bgroup[g].nsub; k++)
            bat_sub_free(&r->bgroup[g].sub[k]);
        free(r->bgroup[g].sub);
    }
    free(r->bgroup);
    r->bgroup = NULL;
    r->nbgroup = 0;
    for (uint32_t i = 0; i < r->nbitem; i++) {
        free(r->bitem[i].key); free(r->bitem[i].sub); free(r->bitem[i].rev); free(r->bitem[i].out); free(r->bitem[i].chg);
    }
    free(r->bitem);
    r->bitem = NULL;
    r->nbitem = 0;
    r->bat_keys = NULL;
    free(r->bat_idx);
    r->bat_idx = NULL;
    r->cbat_idx = 0;
}

/* what a plain part is drawn with, as draw_part would: 0 when the part is not plain */
static int bat_part_key(Renderer *r, const SceneMesh *sm, const MeshPart *p, const Instance *in, double t, BatKey *k)
{
    const Scene *s = r->scene;
    if (!p->ntris || !p->nverts || !p->pos || !p->tris || p->nverts > BAT_MAX_VERTS || 3u * p->ntris > BAT_MAX_INDICES)
        return 0;
    uint32_t name = material_texture(sm->b, p->material), tex = shown_texture(r, sm->b, name, t);
    float reg[4], rs[2];
    if (tex != name)                                 /* an .ifl: its frame changes */
        return 0;
    /* a region's tile is batched as a texture of its own, its scale in the batch's coordinates (bat_build) */
    if (!g_xbox_region_bake && p->uv && material_region(sm->b, p->material, reg))
        return 0;
    GLuint rid = p->uv && material_region(sm->b, p->material, reg) ? region_texture(r, sm->b, tex, reg, rs) : 0;
    if (cube_material(s, sm, p->material))
        return 0;
    if (s->cloud_material && sm->b == s->city && p->material == s->cloud_material)
        return 0;
    memset(k, 0, sizeof *k);
    material_color(sm->b, p->material, k->col);
    /* the blended parts (draw_part: the part's own blend, else its material's; the glass by its colour's alpha) are
     * batched too, in batches of their own drawn after the opaque world (bat_draw_blended) */
    k->blend = (uint8_t)(p->blend ? p->blend : material_blended(sm->b, p->material) ? 2 : k->col[3] < 0.98f ? 3 : 0);
    if (k->blend > 3)
        return 0;
    if (p->col && p->uv && material_damage_layer(sm->b, p->material))
        return 0;
    if (p->nrm && material_env(sm->b, p->material))
        return 0;
    k->tex = rid ? rid : texture(r, sm->b, tex);
    k->fmt = (uint8_t)((p->nrm ? 1 : 0) | (p->uv ? 2 : 0) | (p->col ? 4 : 0));
    k->two_sided = (material_flags(sm->b, p->material) & MAT_TWO_SIDED) != 0;
    k->clamp = k->tex && !rid && material_clamped(sm->b, p->material);   /* (a region's tile repeats: draw_part) */
    k->uncut = material_uncut(sm->b, p->material) != 0;
    if (p->col && k->blend != 3)                     /* the colour is the array's */
        k->col[0] = k->col[1] = k->col[2] = k->col[3] = 1;
    if (in->own_ambient && s->nlights && p->nrm) {   /* lit by the item's ambient (unlit parts: no difference) */
        k->own = 1;
        for (int c = 0; c < 3; c++)                  /* in sixteenths: the adornments' ambients are a ramp by their
                                                      * height, and one batch for each value was hundreds of draws */
            k->amb[c] = (float)(int)(in->ambient[c] * 16.0f + 0.5f) / 16.0f;
    }
    return 1;
}

static int bat_group_of(Renderer *r, const BatKey *k)
{
    for (uint32_t g = 0; g < r->nbgroup; g++)
        if (!memcmp(&r->bgroup[g].key, k, sizeof *k))
            return (int)g;
    BatGroup *n = realloc(r->bgroup, sizeof *n * (r->nbgroup + 1));
    if (!n)
        return -1;
    r->bgroup = n;
    memset(&n[r->nbgroup], 0, sizeof *n);
    n[r->nbgroup].key = *k;
    return (int)r->nbgroup++;
}

/* the batch's arrays from its members, each through its item's matrix */
static void bat_build(Renderer *r, const BatKey *key, BatSub *b)
{
    const Scene *s = r->scene;
    uint32_t nv = 0, ni = 0, live = 0;
    for (uint32_t m = 0; m < b->nmem; m++)               /* the members that left give up their places */
        if (!b->mem[m].gone)
            b->mem[live++] = b->mem[m];
    b->nmem = live;
    for (uint32_t m = 0; m < b->nmem; m++) {
        const MeshPart *p = &s->meshes[s->items[b->mem[m].item].mesh].mesh.parts[b->mem[m].part];
        nv += p->nverts;
        ni += 3 * p->ntris;
    }
    bat_arrays_free(b);
    b->fverts = nv;
    b->fidx = ni;
    if (!nv || !ni)
        return;
    if (ni > b->cidx) {
        free(b->idx);
        b->idx = malloc(sizeof(uint16_t) * ni);
        b->cidx = b->idx ? ni : 0;
    }
    int nrm = key->fmt & 1, uv = key->fmt & 2, col = key->fmt & 4;
    b->pos = gliStaticArrayAlloc(sizeof(float) * 3 * nv, b);
    b->nrm = nrm ? gliStaticArrayAlloc(sizeof(float) * 3 * nv, b) : NULL;
    b->uv = uv ? gliStaticArrayAlloc(sizeof(float) * 2 * nv, b) : NULL;
    b->col = col ? gliStaticArrayAlloc(sizeof(float) * 4 * nv, b) : NULL;
    b->registered = 1;
    if (!b->idx || !b->pos || (nrm && !b->nrm) || (uv && !b->uv) || (col && !b->col)) {
        bat_arrays_free(b);
        b->failed = 1;                               /* its members are drawn part by part (bat_frame) */
        return;
    }
    uint32_t v0 = 0, i0 = 0;
    for (uint32_t m = 0; m < b->nmem; m++) {
        b->mem[m].i0 = i0;
        const DrawItem *it = &s->items[b->mem[m].item];
        const MeshPart *p = &s->meshes[it->mesh].mesh.parts[b->mem[m].part];
        const float *w = it->world.m;
        /* the normals through the matrix of cofactors (the inverse transpose but for its scale: GL_NORMALIZE is on) */
        float c[9] = { w[5] * w[10] - w[6] * w[9], w[6] * w[8] - w[4] * w[10], w[4] * w[9] - w[5] * w[8],
                       w[2] * w[9] - w[1] * w[10], w[0] * w[10] - w[2] * w[8], w[1] * w[8] - w[0] * w[9],
                       w[1] * w[6] - w[2] * w[5], w[2] * w[4] - w[0] * w[6], w[0] * w[5] - w[1] * w[4] };
        float det = w[0] * c[0] + w[1] * c[1] + w[2] * c[2];
        float *po = b->pos + 3 * v0;
        for (uint32_t k = 0; k < p->nverts; k++) {
            float x = p->pos[3 * k], y = p->pos[3 * k + 1], z = p->pos[3 * k + 2];
            po[3 * k] = w[0] * x + w[4] * y + w[8] * z + w[12];
            po[3 * k + 1] = w[1] * x + w[5] * y + w[9] * z + w[13];
            po[3 * k + 2] = w[2] * x + w[6] * y + w[10] * z + w[14];
        }
        if (key->fmt & 1) {
            float *no = b->nrm + 3 * v0;
            for (uint32_t k = 0; k < p->nverts; k++) {
                float x = p->nrm[3 * k], y = p->nrm[3 * k + 1], z = p->nrm[3 * k + 2];
                no[3 * k] = c[0] * x + c[3] * y + c[6] * z;
                no[3 * k + 1] = c[1] * x + c[4] * y + c[7] * z;
                no[3 * k + 2] = c[2] * x + c[5] * y + c[8] * z;
            }
        }
        if (key->fmt & 2) {
            const SceneMesh *sm = &s->meshes[it->mesh];
            float reg[4], rs[2] = { 1, 1 };
            if (material_region(sm->b, p->material, reg) &&
                region_texture(r, sm->b, material_texture(sm->b, p->material), reg, rs)) {
                float *uo = b->uv + 2 * v0;          /* a region's tile: its scale (bat_part_key) */
                for (uint32_t k = 0; k < p->nverts; k++) {
                    uo[2 * k] = p->uv[2 * k] * rs[0];
                    uo[2 * k + 1] = p->uv[2 * k + 1] * rs[1];
                }
            } else
                memcpy(b->uv + 2 * v0, p->uv, sizeof(float) * 2 * p->nverts);
        }
        if (key->fmt & 4)
            memcpy(b->col + 4 * v0, p->col, sizeof(float) * 4 * p->nverts);
        uint16_t *io = b->idx + i0;
        if (det < 0) {                               /* a mirrored item: its triangles the other way round */
            for (uint32_t k = 0; k < p->ntris; k++) {
                io[3 * k] = (uint16_t)(v0 + p->tris[3 * k]);
                io[3 * k + 1] = (uint16_t)(v0 + p->tris[3 * k + 2]);
                io[3 * k + 2] = (uint16_t)(v0 + p->tris[3 * k + 1]);
            }
        } else {
            for (uint32_t k = 0; k < 3 * p->ntris; k++)
                io[k] = (uint16_t)(v0 + p->tris[k]);
        }
        v0 += p->nverts;
        i0 += 3 * p->ntris;
    }
    b->nverts = nv;
    b->nidx = ni;
    b->failed = 0;
}

static void bat_draw(Renderer *r, const BatKey *key, const BatSub *b)
{
    /* Only the members in sight are sent: the batch holds the whole block, and the NV2A takes its indices inline,
     * so drawing all of it every frame cost more than the draws it saved. */
    if (b->nidx > r->cbat_idx) {
        uint16_t *n = realloc(r->bat_idx, sizeof *n * b->nidx);
        if (!n)
            return;
        r->bat_idx = n;
        r->cbat_idx = b->nidx;
    }
    uint32_t nidx = 0;
    for (uint32_t m = 0; m < b->nmem; m++) {
        const BatMember *mm = &b->mem[m];
        if (mm->gone || (mm->item < g_xbox_item_seen_n && !g_xbox_item_seen[mm->item]))
            continue;
        memcpy(r->bat_idx + nidx, b->idx + mm->i0, sizeof *b->idx * mm->ni);
        nidx += mm->ni;
    }
    if (!nidx)
        return;
    if (key->own != r->bat_own || (key->own && memcmp(key->amb, r->bat_amb_now, sizeof key->amb))) {
        const float *a = key->own ? key->amb : r->bat_amb;
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, (float[4]){ a[0], a[1], a[2], 1 });
        r->bat_own = key->own;
        memcpy(r->bat_amb_now, a, sizeof r->bat_amb_now);
    }
    xs_enable(&xs.cull, GL_CULL_FACE, !key->two_sided);
    if (key->tex) {
        xs_bind(key->tex);
        xs_wrap(key->tex, key->clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    } else
        xs_enable(&xs.tex2d, GL_TEXTURE_2D, 0);
    if (!b->nrm) glNormal3f(0, 0, 1);
    if (!b->col) glColor4fv(key->col);
    xs_light(!b->nrm ? 0 : xs_light_base());         /* no normals: its light is in its colours (draw_part) */
    GLboolean test_was = key->uncut && !key->blend ? glIsEnabled(GL_ALPHA_TEST) : GL_FALSE;
    if (test_was)
        glDisable(GL_ALPHA_TEST);
    if (key->blend) {                                /* as draw_part's blended parts */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glAlphaFunc(GL_GREATER, key->blend == 1 ? 0.01f : 0.0f);
        if (key->blend == 1) {
            glDepthMask(GL_FALSE);
            glDepthFunc(GL_LEQUAL);
        }
    }
    XDRAW(0, nidx);
    if (b->registered && gl4es_xbox_predraw())
        gliDirectDraw(b->pos, b->nrm, b->uv, b->col, r->bat_idx, (int)nidx);
    else {                                           /* (gl4es has work to do for this texture) */
        glVertexPointer(3, GL_FLOAT, 0, b->pos);
        xs_client(&xs.nrm, GL_NORMAL_ARRAY, b->nrm != NULL);
        if (b->nrm) glNormalPointer(GL_FLOAT, 0, b->nrm);
        xs_client(&xs.uv, GL_TEXTURE_COORD_ARRAY, b->uv != NULL);
        if (b->uv) glTexCoordPointer(2, GL_FLOAT, 0, b->uv);
        xs_client(&xs.col, GL_COLOR_ARRAY, b->col != NULL);
        if (b->col) glColorPointer(4, GL_FLOAT, 0, b->col);
        if (b->registered)
            gliStaticArraysHint(1);
        glDrawElements(GL_TRIANGLES, (GLsizei)nidx, GL_UNSIGNED_SHORT, r->bat_idx);
        if (b->registered)
            gliStaticArraysHint(0);
    }
    if (key->blend) {
        glDisable(GL_BLEND);
        glAlphaFunc(GL_GREATER, 0.5f);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
    }
    if (test_was)
        glEnable(GL_ALPHA_TEST);
    r->drawn += nidx / 3;
}

/* a part into a batch of its group that has room; the batch is built again at its next draw */
/* returns the vertices this makes bat_frame build again: the whole batch's when it was clean, none when it was
 * already to be built */
static uint32_t bat_join(Renderer *r, uint32_t i, uint32_t k, const MeshPart *p)
{
    BatItem *bi = &r->bitem[i];
    BatGroup *bg = &r->bgroup[bi->key[k]];
    uint32_t nv = p->nverts, ni = 3 * p->ntris;
    int at = -1;
    for (int q = 0; q < bg->nsub && at < 0; q++)
        if (!bg->sub[q].nmem || (bg->sub[q].fverts + nv <= BAT_MAX_VERTS && bg->sub[q].fidx + ni <= BAT_MAX_INDICES))
            at = q;
    if (at < 0) {
        if (bg->nsub >= 32767)
            return 0;
        BatSub *n = realloc(bg->sub, sizeof *n * (size_t)(bg->nsub + 1));
        if (!n)
            return 0;
        bg->sub = n;
        memset(&n[bg->nsub], 0, sizeof *n);
        at = bg->nsub++;
    }
    BatSub *b = &bg->sub[at];
    if (b->nmem == b->cmem) {
        uint32_t c = b->cmem ? 2 * b->cmem : 16;
        BatMember *mm = realloc(b->mem, sizeof *mm * c);
        if (!mm)
            return 0;
        b->mem = mm;
        b->cmem = c;
    }
    uint32_t cost = b->dirty ? 0 : b->fverts + nv;
    BatMember *mm = &b->mem[b->nmem++];
    mm->item = i;
    mm->part = (uint16_t)k;
    mm->nv = nv;
    mm->ni = ni;
    mm->i0 = 0;
    mm->gone = 0;
    b->fverts += nv;
    b->fidx += ni;
    b->dirty = 1;
    bi->sub[k] = (int16_t)at;
    return cost;
}

/* a part out of its batch (it changed, or its item did) */
static void bat_leave(Renderer *r, uint32_t i, uint32_t k)
{
    BatItem *bi = &r->bitem[i];
    if (!bi->sub || bi->sub[k] < 0)
        return;
    if (bi->key[k] >= 0 && (uint32_t)bi->key[k] < r->nbgroup && bi->sub[k] < r->bgroup[bi->key[k]].nsub) {
        BatSub *b = &r->bgroup[bi->key[k]].sub[bi->sub[k]];
        for (uint32_t m = 0; m < b->nmem; m++)
            if (b->mem[m].item == i && b->mem[m].part == k && !b->mem[m].gone) {
                b->mem[m].gone = 1;                  /* (not built again for this: bat_draw passes it over) */
                break;
            }
    }
    bi->sub[k] = -1;
}

static void bat_item_leave(Renderer *r, uint32_t i)
{
    BatItem *bi = &r->bitem[i];
    for (uint32_t k = 0; bi->sub && k < bi->nkeys; k++)
        bat_leave(r, i, k);
}

static int16_t bat_sort_part(Renderer *r, const DrawItem *it, const Instance *in, const Mesh *m, uint32_t k, double t)
{
    const Scene *s = r->scene;
    BatKey bk;
    int g = bat_part_key(r, &s->meshes[it->mesh], &m->parts[k], in, t, &bk) ? bat_group_of(r, &bk) : -1;
    if (g >= 0 && g <= 32767 && it->mesh < r->nmbuf && r->mbuf[it->mesh].pb && r->mbuf[it->mesh].parts == m->parts &&
        k < r->mbuf[it->mesh].nparts)
        xbox_static_unregister(&r->mbuf[it->mesh].pb[k]);   /* (its own copy: the batch has it now) */
    return (int16_t)(g > 32767 ? -1 : g);
}

/* this frame's batches. Each standing item's plain parts are members of batches; a part leaves its batch when it
 * changes (a facade's cell as it is damaged) and comes back a second later, an item leaves with all its parts when
 * it moves, hides or is replaced. Only the batches whose members changed are built again, and all are drawn (the
 * view matrix loaded), each sending the indices of its members in sight. After it Renderer.bitem[i].key says, for
 * an item in the batches, which of its parts draw_mesh leaves out. */
static void bat_frame(Renderer *r, double t)
{
    const Scene *s = r->scene;
    static uint32_t frame;
    frame++;
    int32_t rejoin_left = g_xbox_batch_pace ? (int32_t)BAT_MAX_VERTS : 0x7FFFFFFF;   /* (vertices: see the rejoin) */
    if (r->nbitem != s->nitems) {                    /* another set of items: start again */
        bat_drop(r);
        r->bitem = calloc(s->nitems ? s->nitems : 1, sizeof *r->bitem);
        if (!r->bitem)
            return;
        r->nbitem = s->nitems;
    }
    for (uint32_t i = 0; i < s->nitems; i++) {
        const DrawItem *it = &s->items[i];
        const Instance *in = &s->instances[it->instance];
        BatItem *bi = &r->bitem[i];
        uint8_t was = bi->active;
        bi->active = 0;
        if (in->hidden || it->instance == s->sky || it->instance == s->ground_inst || it->skin || it->mesh >= s->nmeshes) {
            if (was) {
                bat_item_leave(r, i);
                bi->sorted = 0;                      /* (sorted and joined again when it is back) */
            }
            continue;
        }
        const Mesh *m = &s->meshes[it->mesh].mesh;
        int fresh = !bi->seen, moved = memcmp(bi->world, it->world.m, sizeof bi->world) != 0;
        float amb[4] = { in->ambient[0], in->ambient[1], in->ambient[2], (float)(in->own_ambient != 0) };
        if (fresh || moved || bi->parts != m->parts || bi->nparts != m->nparts || bi->mesh != it->mesh ||
            memcmp(bi->amb, amb, sizeof amb)) {
            if (was)
                bat_item_leave(r, i);
            was = 0;
            bi->seen = 1;
            bi->parts = m->parts; bi->nparts = m->nparts; bi->mesh = it->mesh;
            memcpy(bi->world, it->world.m, sizeof bi->world);
            memcpy(bi->amb, amb, sizeof amb);
            bi->sorted = 0;
            if (fresh)
                bi->settle = frame;                  /* (a new block: at once) */
            else if (moved) {                        /* a mover: out for longer each time, up to a minute */
                bi->settle = frame + (BAT_SETTLE_FRAMES << (bi->moves < 6 ? bi->moves : 6));
                if (bi->moves < 255) bi->moves++;
            } else
                bi->settle = frame + BAT_SETTLE_FRAMES;
        }
        if ((int32_t)(frame - bi->settle) < 0)
            continue;                                /* it changed a moment ago: part by part */
        if (!bi->sorted) {
            uint32_t n = m->nparts ? m->nparts : 1;
            int16_t *key = realloc(bi->key, sizeof *key * n);
            if (key) bi->key = key;
            int16_t *sub = realloc(bi->sub, sizeof *sub * n);
            if (sub) bi->sub = sub;
            uint32_t *rev = realloc(bi->rev, sizeof *rev * n);
            if (rev) bi->rev = rev;
            uint32_t *out = realloc(bi->out, sizeof *out * n);
            if (out) bi->out = out;
            uint8_t *chg = realloc(bi->chg, n);
            if (chg) bi->chg = chg;
            if (!key || !sub || !rev || !out || !chg) {
                bi->nkeys = 0;
                continue;
            }
            bi->nkeys = m->nparts;
            for (uint32_t k = 0; k < m->nparts; k++) {
                key[k] = bat_sort_part(r, it, in, m, k, t);
                sub[k] = -1;
                rev[k] = m->parts[k].rev;
                out[k] = 0;
                chg[k] = 0;
                if (key[k] >= 0)
                    bat_join(r, i, k, &m->parts[k]);
            }
            bi->sorted = 1;
        } else {
            for (uint32_t k = 0; k < m->nparts; k++) {
                if (m->parts[k].rev != bi->rev[k]) {         /* this part changed: out of its batch for a second */
                    bi->rev[k] = m->parts[k].rev;
                    bat_leave(r, i, k);
                    bi->key[k] = -1;
                    bi->out[k] = frame + (BAT_SETTLE_FRAMES << (bi->chg[k] < 5 ? bi->chg[k] : 5));   /* longer each time */
                    if (!bi->out[k]) bi->out[k] = 1;
                    if (bi->chg[k] < 255) bi->chg[k]++;
                } else if (bi->out[k] && (int32_t)(frame - bi->out[k]) >= 0) {
                    /* Back into its batch, which is then built again whole (every member's vertices through its
                     * matrix: up to 10,000). With buildings under attack parts came back every frame, each into
                     * another batch, and those rebuilds were the largest single cost of the busiest frames. They
                     * come back together instead: in four frames of every sixteen, and no more than one batch's
                     * worth of vertices a frame; parts for a batch already to be built ride along free. A part
                     * that waits is drawn on its own meanwhile, as it has been since it changed. */
                    if (rejoin_left <= 0 || (frame & 15u) > 3u) {
                        bi->out[k] = frame + 1 ? frame + 1 : 1;
                        continue;
                    }
                    bi->out[k] = 0;
                    bi->key[k] = bat_sort_part(r, it, in, m, k, t);
                    if (bi->key[k] >= 0)
                        rejoin_left -= (int32_t)bat_join(r, i, k, &m->parts[k]);
                }
            }
        }
        bi->active = 1;
    }
    for (uint32_t g = 0; g < r->nbgroup; g++) {
        BatGroup *bg = &r->bgroup[g];
        for (int k = 0; k < bg->nsub; k++) {
            BatSub *b = &bg->sub[k];
            if (b->dirty) {
                b->dirty = 0;
                bat_build(r, &bg->key, b);
                if (b->failed) {                     /* no room for it: its members are drawn part by part from now
                                                      * on (until their items change and are sorted again) */
                    for (uint32_t m = 0; m < b->nmem; m++) {
                        BatItem *bi = &r->bitem[b->mem[m].item];
                        bi->key[b->mem[m].part] = -1;
                        bi->sub[b->mem[m].part] = -1;
                    }
                    b->nmem = 0;
                    b->fverts = b->fidx = 0;
                    b->failed = 0;
                }
            }
            if (b->nidx && !bg->key.blend && !XSKIP(4))
                bat_draw(r, &bg->key, b);
        }
    }
    if (r->bat_own) {                                /* the scene's ambient again */
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, (float[4]){ r->bat_amb[0], r->bat_amb[1], r->bat_amb[2], 1 });
        r->bat_own = 0;
    }
}

/* the blended batches, after the opaque world is drawn (the view matrix loaded): the cracks and the facades' sheets
 * lie on walls that are there by now; among themselves they are drawn in no particular order, as the parts were */
static void bat_draw_blended(Renderer *r)
{
    if (XSKIP(8))
        return;
    for (uint32_t g = 0; g < r->nbgroup; g++) {
        BatGroup *bg = &r->bgroup[g];
        if (!bg->key.blend)
            continue;
        for (int k = 0; k < bg->nsub; k++)
            if (bg->sub[k].nidx)
                bat_draw(r, &bg->key, &bg->sub[k]);
    }
    if (r->bat_own) {
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, (float[4]){ r->bat_amb[0], r->bat_amb[1], r->bat_amb[2], 1 });
        r->bat_own = 0;
    }
}

/* ---- a moving model of many rigid pieces in a few draws ---------------------------------------------------------
 * A person, a soldier, a car is a node tree with a mesh hung on each node: an item, a matrix and a draw for every
 * piece, and in a street half of all the items are people's limbs. They move every frame, so the batches of the
 * standing city never take them. Here the pieces of one model are merged into one mesh whose vertices each name
 * their piece as their single bone, and drawn with the skinning program (gliSkinDraw: the two-bone one, the second
 * weight 0), the pieces' matrices as the palette: a draw for each texture the model uses in place of one for each
 * piece. The prototype draws its rigid shapes piece by piece and has no crowd; this is the port's own, on the
 * prototype's skinning. The merged mesh is made once for a model (its meshes' list) and shared by every instance
 * of it. A part the batches would not take (a second layer, an animated texture, a blended one) is left to the
 * ordinary draw of its item. */
#define RIG_MAX 96                                   /* models with merged meshes (all dropped with the scene) */
#define RIG_SUBS 6                                   /* textures a model's pieces are drawn with */
typedef struct {
    BatKey key;
    float *pos, *nrm, *uv, *col, *weight;
    int16_t *bone;
    uint16_t *idx;
    uint32_t nverts, nidx;
} RigSub;
typedef struct RigMesh {
    uint32_t sig, n;                                 /* its items' signature and count (at most SKIN_GROUP_BONES) */
    uint32_t *mesh;                                  /* their meshes */
    const MeshPart **parts;                          /* and each one's parts and their count, to tell a change */
    uint32_t *nparts, *rev;
    int16_t **keys;                                  /* per item, per part: 0 merged (draw_mesh leaves it), -1 not */
    uint8_t *left;                                   /* per item: it has parts that are not merged */
    RigSub sub[RIG_SUBS];
    int nsub, failed;
} RigMesh;

/* its merged meshes out of the store (a failed one keeps its place in the list, so it is not tried again, and
 * holds no GPU memory) */
static void rig_release(RigMesh *rm)
{
    for (int q = 0; q < rm->nsub; q++) {
        RigSub *b = &rm->sub[q];
        if (b->pos) gliStaticArrayRemove(b->pos, rm);
        if (b->nrm) gliStaticArrayRemove(b->nrm, rm);
        if (b->uv) gliStaticArrayRemove(b->uv, rm);
        if (b->col) gliStaticArrayRemove(b->col, rm);
        if (b->weight) gliStaticArrayRemove(b->weight, rm);
        if (b->bone) gliStaticArrayRemove(b->bone, rm);
        free(b->idx);
        memset(b, 0, sizeof *b);
    }
    rm->nsub = 0;
}

static void rig_free(RigMesh *rm)
{
    rig_release(rm);
    for (uint32_t j = 0; rm->keys && j < rm->n; j++)
        free(rm->keys[j]);
    free(rm->keys); free(rm->left); free(rm->mesh); free(rm->parts); free(rm->nparts); free(rm->rev);
    memset(rm, 0, sizeof *rm);
}

static void rig_drop(Renderer *r)
{
    for (int k = 0; k < r->nrig; k++)
        rig_free(&r->rig[k]);
    free(r->rig);
    r->rig = NULL;
    r->nrig = 0;
}

static uint32_t rig_sig(const Scene *s, uint32_t i0, uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t j = 0; j < n; j++) {
        const Mesh *m = &s->meshes[s->items[i0 + j].mesh].mesh;
        h = (h ^ s->items[i0 + j].mesh) * 16777619u;
        h = (h ^ (uint32_t)(uintptr_t)m->parts) * 16777619u;
        h = (h ^ m->nparts) * 16777619u;
        for (uint32_t k = 0; k < m->nparts; k++)
            h = (h ^ m->parts[k].rev) * 16777619u;
    }
    return h;
}

/* the model the entry was made for: the same meshes in the same order (what they hold is the signature's) */
static int rig_same(const Scene *s, const RigMesh *rm, uint32_t i0, uint32_t n)
{
    if (rm->n != n || !rm->mesh)
        return 0;
    for (uint32_t j = 0; j < n; j++)
        if (rm->mesh[j] != s->items[i0 + j].mesh)
            return 0;
    return 1;
}

/* the merged mesh of the items i0 .. i0 + n (one instance's, unskinned, at most SKIN_GROUP_BONES), made at its
 * first draw; NULL: not this model (no memory, no room in the store, nothing of it could be merged) */
static RigMesh *rig_mesh(Renderer *r, uint32_t i0, uint32_t n, double t)
{
    const Scene *s = r->scene;
    uint32_t sig = rig_sig(s, i0, n);
    for (int k = 0; k < r->nrig; k++) {
        RigMesh *e = &r->rig[k];
        if (!rig_same(s, e, i0, n))
            continue;
        if (e->sig != sig && !e->failed) {           /* its meshes changed since (a part's rev, its parts): a model
                                                      * that changes as it moves is not merged, and holds nothing */
            rig_release(e);
            e->failed = 1;
        }
        return e->failed ? NULL : e;
    }
    if (r->nrig == RIG_MAX)                          /* (every model of a long level: start again) */
        rig_drop(r);
    if (!r->rig && !(r->rig = calloc(RIG_MAX, sizeof *r->rig)))
        return NULL;
    RigMesh *rm = &r->rig[r->nrig++];
    memset(rm, 0, sizeof *rm);
    rm->sig = sig;
    rm->n = n;
    rm->failed = 1;                                  /* until it is whole */
    rm->mesh = calloc(n, sizeof *rm->mesh);
    rm->parts = calloc(n, sizeof *rm->parts);
    rm->nparts = calloc(n, sizeof *rm->nparts);
    rm->keys = calloc(n, sizeof *rm->keys);
    rm->left = calloc(n, 1);
    if (!rm->mesh || !rm->parts || !rm->nparts || !rm->keys || !rm->left) {
        rig_free(rm);                                /* (no entry at all: its lists would be read by the lookup) */
        r->nrig--;
        return NULL;
    }
    uint32_t nv[RIG_SUBS] = { 0 }, ni[RIG_SUBS] = { 0 };
    int overlay = 0;
    for (uint32_t j = 0; j < n; j++) {               /* (the whole list first: the lookup compares it) */
        const Mesh *m = &s->meshes[s->items[i0 + j].mesh].mesh;
        rm->mesh[j] = s->items[i0 + j].mesh;
        rm->parts[j] = m->parts;
        rm->nparts[j] = m->nparts;
    }
    for (uint32_t j = 0; j < n; j++) {               /* which parts go into which merged mesh */
        const DrawItem *it = &s->items[i0 + j];
        const SceneMesh *sm = &s->meshes[it->mesh];
        const Mesh *m = &sm->mesh;
        if (!(rm->keys[j] = malloc(sizeof **rm->keys * (m->nparts ? m->nparts : 1))))
            return NULL;                             /* (it stays failed, with nothing in the store) */
        for (uint32_t k = 0; k < m->nparts; k++) {
            const MeshPart *p = &m->parts[k];
            BatKey bk;
            rm->keys[j][k] = -1;
            if (!p->ntris)
                continue;
            int q = -1, plain = bat_part_key(r, sm, p, &s->instances[it->instance], t, &bk);
            if (plain && bk.blend == 1)
                overlay = 1;
            if (plain && !bk.blend && p->nverts <= 65535u) {
                bk.own = 0;                          /* (the instance's ambient is set when it is drawn) */
                bk.amb[0] = bk.amb[1] = bk.amb[2] = 0;
                for (q = 0; q < rm->nsub; q++)
                    if (!memcmp(&rm->sub[q].key, &bk, sizeof bk))
                        break;
                if (q == rm->nsub && rm->nsub < RIG_SUBS)
                    rm->sub[rm->nsub++].key = bk;
                else if (q == rm->nsub)
                    q = -1;
                if (q >= 0 && (nv[q] + p->nverts > 65535u || nv[q] + p->nverts > SKIN_GROUP_VERTS))
                    q = -1;
            }
            if (q < 0) {
                rm->left[j] = 1;
                continue;
            }
            rm->keys[j][k] = (int16_t)(q + 1);       /* (its merged mesh for now; 0 once they are made) */
            nv[q] += p->nverts;
            ni[q] += 3 * p->ntris;
        }
    }
    /* A model with a crack layer (drawn at the depth of the wall under it, LEQUAL) is left as it is: the merged wall
     * would come from the vertex program and the cracks from the fixed pipeline, and their depths need not agree
     * to the last bit. */
    if (overlay) {
        rm->nsub = 0;
        return NULL;
    }
    int any = 0;
    for (int q = 0; q < rm->nsub; q++) {
        RigSub *b = &rm->sub[q];
        if (!nv[q])
            continue;
        int nrm = b->key.fmt & 1, uv = b->key.fmt & 2, col = b->key.fmt & 4;
        b->pos = gliStaticArrayAlloc(sizeof(float) * 3 * nv[q], rm);
        b->nrm = nrm ? gliStaticArrayAlloc(sizeof(float) * 3 * nv[q], rm) : NULL;
        b->uv = uv ? gliStaticArrayAlloc(sizeof(float) * 2 * nv[q], rm) : NULL;
        b->col = col ? gliStaticArrayAlloc(sizeof(float) * 4 * nv[q], rm) : NULL;
        b->bone = gliStaticArrayAlloc(sizeof(int16_t) * 4 * nv[q], rm);
        b->weight = gliStaticArrayAlloc(sizeof(float) * 4 * nv[q], rm);
        b->idx = malloc(sizeof *b->idx * ni[q]);
        if (!b->pos || (nrm && !b->nrm) || (uv && !b->uv) || (col && !b->col) || !b->bone || !b->weight || !b->idx) {
            rig_release(rm);                         /* no room: it stays failed, and holds nothing */
            return NULL;
        }
        /* each array written once and in order: the store is write-combined */
#define RIG_EACH(body) \
        for (uint32_t j = 0; j < n; j++) { \
            const SceneMesh *sm = &s->meshes[s->items[i0 + j].mesh]; \
            for (uint32_t k = 0; k < sm->mesh.nparts; k++) { \
                const MeshPart *p = &sm->mesh.parts[k]; \
                if (rm->keys[j][k] != q + 1) continue; \
                body \
            } \
        }
        float *o = b->pos;
        RIG_EACH(memcpy(o, p->pos, sizeof(float) * 3 * p->nverts); o += 3 * p->nverts;)
        if (nrm) {
            o = b->nrm;
            RIG_EACH(memcpy(o, p->nrm, sizeof(float) * 3 * p->nverts); o += 3 * p->nverts;)
        }
        if (uv) {
            o = b->uv;
            RIG_EACH(
                float reg[4]; float rs[2]; rs[0] = rs[1] = 1;
                if (material_region(sm->b, p->material, reg) &&
                    region_texture(r, sm->b, material_texture(sm->b, p->material), reg, rs)) {
                    for (uint32_t v = 0; v < p->nverts; v++) {
                        o[2 * v] = p->uv[2 * v] * rs[0];
                        o[2 * v + 1] = p->uv[2 * v + 1] * rs[1];
                    }
                } else
                    memcpy(o, p->uv, sizeof(float) * 2 * p->nverts);
                o += 2 * p->nverts;)
        }
        if (col) {
            o = b->col;
            RIG_EACH(memcpy(o, p->col, sizeof(float) * 4 * p->nverts); o += 4 * p->nverts;)
        }
        int16_t *bo = b->bone;
        RIG_EACH(
            for (uint32_t v = 0; v < p->nverts; v++) {
                bo[4 * v] = (int16_t)(3 * j); bo[4 * v + 1] = 0; bo[4 * v + 2] = 0; bo[4 * v + 3] = 0;
            }
            bo += 4 * p->nverts;)
        o = b->weight;
        RIG_EACH(
            for (uint32_t v = 0; v < p->nverts; v++) {
                o[4 * v] = 1; o[4 * v + 1] = 0; o[4 * v + 2] = 0; o[4 * v + 3] = 0;
            }
            o += 4 * p->nverts;)
        uint16_t *io = b->idx;
        uint32_t v0 = 0;
        RIG_EACH(
            for (uint32_t x = 0; x < 3 * p->ntris; x++)
                io[x] = (uint16_t)(v0 + p->tris[x]);
            io += 3 * p->ntris;
            v0 += p->nverts;)
#undef RIG_EACH
        b->nverts = nv[q];
        b->nidx = ni[q];
        any = 1;
    }
    for (uint32_t j = 0; j < n; j++)                 /* draw_mesh's convention: 0 and up is "not drawn here" */
        for (uint32_t k = 0; k < rm->nparts[j]; k++)
            if (rm->keys[j][k] > 0)
                rm->keys[j][k] = 0;
    rm->failed = !any;
    return any ? rm : NULL;
}

/* the merged meshes of the items i0 .. i0 + n with their matrices as the palette (the view matrix is loaded).
 * 0: gl4es has work for one of the textures; nothing more is drawn this way for this model. */
static int rig_draw(Renderer *r, RigMesh *rm, uint32_t i0)
{
    const Scene *s = r->scene;
    static float pal[SKIN_GROUP_BONES][12];
    for (uint32_t j = 0; j < rm->n; j++) {
        const float *m = s->items[i0 + j].world.m;
        for (int row = 0; row < 3; row++) {          /* its rows (the matrices are column-major) */
            pal[j][4 * row] = m[row];
            pal[j][4 * row + 1] = m[4 + row];
            pal[j][4 * row + 2] = m[8 + row];
            pal[j][4 * row + 3] = m[12 + row];
        }
    }
    for (int q = 0; q < rm->nsub; q++) {
        const RigSub *b = &rm->sub[q];
        const BatKey *key = &b->key;
        if (!b->nidx)
            continue;
        xs_enable(&xs.cull, GL_CULL_FACE, !key->two_sided);
        if (key->tex) {
            xs_bind(key->tex);
            xs_wrap(key->tex, key->clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        } else
            xs_enable(&xs.tex2d, GL_TEXTURE_2D, 0);
        if (!b->nrm) glNormal3f(0, 0, 1);
        if (!b->col) glColor4fv(key->col);
        xs_light(!b->nrm ? 0 : xs_light_base());     /* no normals: its light is in its colours (draw_part) */
        GLboolean test_was = key->uncut ? glIsEnabled(GL_ALPHA_TEST) : GL_FALSE;
        if (test_was)
            glDisable(GL_ALPHA_TEST);
        int ok = gl4es_xbox_predraw();
        XDRAW(1, b->nidx);
        if (ok)
            gliSkinDraw(b->pos, b->nrm, b->uv, b->col, b->bone, b->weight, b->idx, (int)b->nidx,
                        (const float (*)[12])pal, (int)rm->n, 0);
        if (test_was)
            glEnable(GL_ALPHA_TEST);
        if (!ok) {                                   /* (a texture gl4es must work on, every frame: not this model) */
            rm->failed = 1;
            rig_release(rm);                         /* (freed once the GPU is idle: the draws made are queued) */
            return 0;
        }
        r->drawn += b->nidx / 3;
    }
    return 1;
}
#endif

#ifdef RAMPAGE_XBOX
/* The quads of the effects and of the HUD, gathered and drawn as arrays: a glBegin .. glEnd for each one went through
 * gl4es's display-list machinery, a few dozen HUD quads costing as much as a hundred parts of the city. Quads are
 * added in their order and drawn when the state they need changes (the caller's qb_flush), so what is blended over
 * what is as before. */
#define QB_MAX 1024
/* A run of quads (from one flush to the next) goes one of two ways. When gl4es has nothing to do for the bound
 * texture (gl4es_xbox_predraw, asked at the run's first vertex: the state is set by then), its vertices are written
 * straight into GPU memory, 24 bytes each as the prototype's effects and HUD (position, colour bytes, texture
 * coordinates: gliDirectQuads), and drawn from there with no indices. Else, as before, into arrays here that are
 * drawn through glDrawElements and the staging arena.
 * The GPU memory is a chunk of the static store filled from its start and never rewritten: a frame still queued may
 * be reading what was drawn. A full chunk is released (freed once the GPU is idle) and another taken. */
#define QB_VERTEX 24u
#define QB_CHUNK (96u * 1024u)                       /* 1,024 quads */
#define QB_RING 8                                    /* chunks at most: three frames of 2,700 quads */
void gliDirectQuads(const void *verts, int nverts);
static struct {
    float pos[QB_MAX * 4 * 3], uv[QB_MAX * 4 * 2], col[QB_MAX * 4 * 4];
    uint16_t idx[QB_MAX * 6];
    int n, ready;
    int direct;                                      /* this run's vertices go into the chunk */
    uint8_t *chunk;                                  /* the chunk being filled, NULL: none */
    uint32_t chunk_at, run_at;                       /* its first free byte; where this run starts */
    /* The chunks are the batcher's own and go round: a full one is not given back to the store and another taken
     * (that was a release and an allocation of 96 KB for every thousand quads, some of them every frame, which
     * left holes among the store's long-lived arrays and kept it a megabyte or two larger than what it held). A
     * chunk is filled again once two frames have ended since it was last drawn from: the GPU is never more than
     * one frame behind what has been sent (nxdk-gles11's frame fence; with the short push buffer it is idle at
     * every frame's start). */
    struct { uint8_t *mem; uint32_t frame; } ring[QB_RING];
    int ring_n, ring_at;                             /* chunks owned; the one being filled */
    uint32_t frame;                                  /* frames begun (qb_frame) */
} qb;

/* a frame begins (rnd_frame) */
static void qb_frame(void) { qb.frame++; }

static void qb_flush(void)
{
    if (!qb.n)
        return;
    XDRAW(2, qb.n * 4);
    if (qb.direct) {
        gliDirectQuads(qb.chunk + qb.run_at, qb.n * 4);
        qb.chunk_at = qb.run_at + (uint32_t)qb.n * 4u * QB_VERTEX;
    } else {
        glVertexPointer(3, GL_FLOAT, 0, qb.pos);
        glTexCoordPointer(2, GL_FLOAT, 0, qb.uv);
        glColorPointer(4, GL_FLOAT, 0, qb.col);
        glDrawElements(GL_TRIANGLES, qb.n * 6, GL_UNSIGNED_SHORT, qb.idx);
    }
    qb.n = 0;
}
static void qb_begin(void)
{
    if (!qb.ready) {
        for (int i = 0; i < QB_MAX; i++) {
            uint16_t *o = &qb.idx[6 * i], v = (uint16_t)(4 * i);
            o[0] = v; o[1] = (uint16_t)(v + 1); o[2] = (uint16_t)(v + 2);
            o[3] = v; o[4] = (uint16_t)(v + 2); o[5] = (uint16_t)(v + 3);
        }
        qb.ready = 1;
    }
    qb.n = 0;
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
}
static void qb_end(void)
{
    qb_flush();
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
}
/* a run's first vertex: where the run goes */
static void qb_run(void)
{
    qb.direct = 0;
    if (!g_xbox_direct_quads || !gl4es_xbox_predraw())
        return;
    if (qb.chunk)
        qb.ring[qb.ring_at].frame = qb.frame;        /* drawn from in this frame */
    if (qb.chunk && qb.chunk_at + 4u * QB_VERTEX > QB_CHUNK)
        qb.chunk = NULL;                             /* full: it rests until the GPU is past it */
    if (!qb.chunk) {
        int pick = -1;
        for (int i = 0; i < qb.ring_n && pick < 0; i++)
            if ((uint32_t)(qb.frame - qb.ring[i].frame) >= 2u)
                pick = i;
        if (pick < 0 && qb.ring_n < QB_RING) {
            uint8_t *mem = gliStaticArrayAlloc(QB_CHUNK, &qb);
            if (mem) {
                pick = qb.ring_n++;
                qb.ring[pick].mem = mem;
            }
        }
        if (pick < 0)
            return;                                  /* none free yet, no room for one more: the arrays here */
        qb.ring_at = pick;
        qb.ring[pick].frame = qb.frame;
        qb.chunk = qb.ring[pick].mem;
        qb.chunk_at = 0;
    }
    qb.run_at = qb.chunk_at;
    qb.direct = 1;
}
static inline uint8_t qb_byte(float v)
{
    return v <= 0 ? 0 : v >= 1 ? 255 : (uint8_t)(v * 255.0f + 0.5f);
}
/* the next quad's corner k (0 .. 3, in order): its place, texture coordinates and colour */
/* (in line at each of its calls: four of them a quad, a thousand quads and more a frame) */
static inline __attribute__((always_inline)) void qb_vertex(int k, float x, float y, float z, float u, float v,
                                                          const float rgb[3], float a)
{
    if (!qb.n && !k)
        qb_run();
    if (qb.direct) {                                 /* (write-combined memory: each field once, in order) */
        uint8_t *o = qb.chunk + qb.run_at + (uint32_t)(qb.n * 4 + k) * QB_VERTEX;
        float *f = (float *)(void *)o;
        f[0] = x; f[1] = y; f[2] = z;
        o[12] = qb_byte(rgb[0]); o[13] = qb_byte(rgb[1]); o[14] = qb_byte(rgb[2]); o[15] = qb_byte(a);
        f[4] = u; f[5] = v;
        return;
    }
    int at = qb.n * 4 + k;
    float *p = &qb.pos[3 * at], *t = &qb.uv[2 * at], *c = &qb.col[4 * at];
    p[0] = x; p[1] = y; p[2] = z;
    t[0] = u; t[1] = v;
    c[0] = rgb[0]; c[1] = rgb[1]; c[2] = rgb[2]; c[3] = a;
}
static void qb_next(void)
{
    qb.n++;
    if (qb.direct ? qb.run_at + (uint32_t)(qb.n + 1) * 4u * QB_VERTEX > QB_CHUNK : qb.n == QB_MAX)
        qb_flush();
}
#endif

#ifdef RAMPAGE_XBOX
/* view x world in one load (the prototype's one matrix an object), not a push, a multiply and a pop through gl4es */
static void xbox_item_matrix(const float *a, const float *b)
{
    float mv[16];
    for (int c = 0; c < 4; c++)
        for (int q = 0; q < 4; q++)
            mv[4 * c + q] = a[q] * b[4 * c] + a[4 + q] * b[4 * c + 1] + a[8 + q] * b[4 * c + 2] + a[12 + q] * b[4 * c + 3];
    glLoadMatrixf(mv);
}
#endif

/* Every texture the scene's meshes are drawn with, made now (a level's loading) and not at its first sight in
 * play: decoding a PS2 texture, building its palette and waiting twice for the GPU in the middle of a fight was
 * a hitch each time. The prototype has all of a level's textures in memory when the level starts
 * (docs/xbox_proto/SOUND_LOADING_FRAME.md). Nothing here is drawn. */
void rnd_preload(Renderer *r)
{
#ifdef RAMPAGE_XBOX
    const Scene *s = r ? r->scene : NULL;
    if (!s || !s->meshes || !g_xbox_preload)
        return;
    unsigned made = 0;
    for (uint32_t m = 0; m < s->nmeshes; m++) {
        const SceneMesh *sm = &s->meshes[m];
        if (!sm->b)
            continue;                                /* (a generated facade: its sheets are the bundles' parts') */
        for (uint32_t k = 0; k < sm->mesh.nparts; k++) {
            const MeshPart *p = &sm->mesh.parts[k];
            if (!p->ntris)
                continue;
            uint32_t name = material_texture(sm->b, p->material), tex = shown_texture(r, sm->b, name, 0);
            float reg[4], rs[2];
            if (p->uv && material_region(sm->b, p->material, reg))
                made += region_texture(r, sm->b, tex, reg, rs) != 0;
            else
                made += texture(r, sm->b, tex) != 0;
            for (int f = 0; f < r->nifl; f++)        /* an animated one: all its frames */
                if (r->ifl[f].b == sm->b && r->ifl[f].name == name)
                    for (int q = 0; q < r->ifl[f].n; q++)
                        made += texture(r, sm->b, r->ifl[f].frame[q]) != 0;
            uint32_t layer = p->col && p->uv ? material_damage_layer(sm->b, p->material) : 0;
            if (layer)
                made += texture(r, sm->b, layer) != 0;
            layer = p->nrm ? material_env(sm->b, p->material) : 0;
            if (layer)
                made += texture(r, sm->b, layer) != 0;
        }
    }
    if (s->city && s->env_map)
        made += texture(r, s->city, s->env_map) != 0;
    XS_TEXTURE_BOUND();
    printf("xbox preload: %u textures for %u meshes\n", made, (unsigned)s->nmeshes);
    bdp_payload_release();                           /* (the last mesh read from the disc for the level's building) */
#else
    (void)r;
#endif
}

static void draw_held(Renderer *r, double t)
{
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    for (int k = 0; k < r->nheld; k++) {
        glLoadMatrixf(r->held[k].mv);
        draw_part(r, r->held[k].sm, r->held[k].skin, r->held[k].part, r->held[k].first, t);
    }
    glPopMatrix();
    r->nheld = 0;
    XS_END();
}

#ifdef RAMPAGE_XBOX
/* how a part is drawn: on the GPU's skinning program (sg), straight from its arrays in GPU memory (ok), or through
 * glDrawElements with client arrays (set when first needed) */
typedef struct {
    const SkinGpu *sg;
    const Skin *skin;
    int ok, arrays_set;
    const float *pos, *nrm, *uv, *col;               /* in GPU memory */
    const uint16_t *idx16;
    uint32_t nidx16;                                 /* the indices of the copies in GPU memory (a changed part's
                                                      * old copies have their own count) */
    int baked;                                       /* the copies' texture coordinates hold the region's scale rs:
                                                      * a draw from the client arrays sets the texture matrix */
    float rs[2];
    const float *cpos, *cnrm, *cuv, *ccol;           /* the client arrays */
    int sphere;                                      /* the texture goes on at the sphere-map coordinates: made
                                                      * by the GPU on the direct path (gliDirectSphereMap), by
                                                      * gl4es otherwise (sphere_coords) */
} XboxDirect;

static void sphere_coords(int on);
extern void gliDirectSphereMap(GLboolean on, GLfloat ks, GLfloat kt);
int g_xbox_gpu_sphere = 1;               /* gpusphere: a reflection's coordinates from the GPU (--xoff gpusphere:
                                          * from gl4es, each vertex each frame, as before) */

static void xbox_client_arrays(XboxDirect *d)
{
    glVertexPointer(3, GL_FLOAT, 0, d->cpos);
    xs_client(&xs.nrm, GL_NORMAL_ARRAY, d->cnrm != NULL);
    if (d->cnrm) glNormalPointer(GL_FLOAT, 0, d->cnrm);
    xs_client(&xs.uv, GL_TEXTURE_COORD_ARRAY, d->cuv != NULL);
    if (d->cuv) glTexCoordPointer(2, GL_FLOAT, 0, d->cuv);
    xs_client(&xs.col, GL_COLOR_ARRAY, d->ccol != NULL);
    if (d->ccol) glColorPointer(4, GL_FLOAT, 0, d->ccol);
    d->arrays_set = 1;
}

/* a skinned part's groups, each with its bones' matrices from the instance's pose */
static void xbox_skin_draw(Renderer *r, const SkinGpu *sg)
{
    if (!r->pal && !(r->pal = malloc(sizeof *r->pal * 256)))
        return;
    if (r->pal_inst != r->cur_inst || r->pal_frame != r->xframe) {
        r->npal = scene_skin_palette(r->cur_inst, r->pal);
        r->pal_inst = r->cur_inst;
        r->pal_frame = r->xframe;
    }
    for (int k = 0; k < sg->ng; k++) {
        const SkinGroup *g = &sg->g[k];
        static float gp[SKIN_GROUP_BONES][12];
        for (int j = 0; j < g->nb; j++) {
            static const float one[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
            const float *m = g->b[j] < r->npal ? r->pal[g->b[j]].m : one;
            for (int row = 0; row < 3; row++) {      /* its rows (the matrices are column-major) */
                gp[j][4 * row] = m[row];
                gp[j][4 * row + 1] = m[4 + row];
                gp[j][4 * row + 2] = m[8 + row];
                gp[j][4 * row + 3] = m[12 + row];
            }
        }
        XDRAW(3, g->nidx);
        gliSkinDraw(g->pos, g->nrm, g->uv, g->col, g->bone, g->weight, g->idx, (int)g->nidx,
                    (const float (*)[12])gp, g->nb, g->four);
    }
}

/* A skin goes to the GPU whole or not at all (the prototype has no CPU skinning beside it): every part must be
 * one the program can draw. One part with a second layer or a texture matrix used to send the skin back to the
 * CPU in the middle of a frame, with the copies made for the parts before it left in the store. */
static int xbox_skin_ok(Renderer *r, const SceneMesh *sm, const Skin *skin)
{
    const Scene *s = r->scene;
    float reg[4];
    if (!skin->bones || !skin->weights || gliSkinMaxBones() < 12)
        return 0;
    for (uint32_t k = 0; k < sm->mesh.nparts; k++) {
        const MeshPart *p = &sm->mesh.parts[k];
        if (!p->ntris)
            continue;
        if (!p->nrm || !p->pos || !p->tris || !p->nverts || (p->uv && material_region(sm->b, p->material, reg)) ||
            cube_material(s, sm, p->material) || material_env(sm->b, p->material) ||
            (p->col && p->uv && material_damage_layer(sm->b, p->material)) ||
            (s->cloud_material && sm->b == s->city && p->material == s->cloud_material))
            return 0;
    }
    return 1;
}

static void xbox_skin_demote(Renderer *r, const SceneMesh *sm, const Skin *skin)
{
    const Scene *s = r->scene;
    ((Skin *)skin)->gpu_skin = -1;
    if (!s || !s->meshes || (uintptr_t)sm < (uintptr_t)s->meshes || (uintptr_t)sm >= (uintptr_t)(s->meshes + s->nmeshes))
        return;
    uint32_t mi = (uint32_t)(sm - s->meshes);
    if (mi >= r->nmbuf || !r->mbuf[mi].pb)
        return;
    for (uint32_t k = 0; k < r->mbuf[mi].nparts; k++) {
        skin_gpu_free(r->mbuf[mi].pb[k].sg);
        r->mbuf[mi].pb[k].sg = NULL;
    }
}

#ifdef RAMPAGE_XBOX_TEST_ARGS
static struct { const char *tex; int env, scroll, rid, skin, own, stale; } xslow;   /* (draw_part's, for the line below) */
#endif

static void xbox_part_draw(Renderer *r, XboxDirect *d, GLsizei count, GLenum idx_type, const void *idx)
{
    if (d->sg) {
        if (gl4es_xbox_predraw())
            xbox_skin_draw(r, d->sg);
        else if (d->skin)                            /* its texture needs gl4es: the CPU poses it from now on */
            ((Skin *)d->skin)->gpu_skin = -1;
        return;
    }
    if (d->ok && (!d->sphere || (g_xbox_gpu_sphere && d->nrm)) && gl4es_xbox_predraw()) {
        XDRAW(4, d->nidx16 ? d->nidx16 : count);
        if (d->sphere)                               /* (sphere_coords' scale: k and -k about the middle) */
            gliDirectSphereMap(GL_TRUE, 0.3394f * 2.828427f, -0.3394f * 2.828427f);
        gliDirectDraw(d->pos, d->nrm, d->uv, d->col, d->idx16, d->nidx16 ? (int)d->nidx16 : count);
        if (d->sphere)
            gliDirectSphereMap(GL_FALSE, 0, 0);
        return;
    }
    XDRAW(5, count);
#ifdef RAMPAGE_XBOX_TEST_ARGS
    {                                                /* test builds: what goes this way, each texture said once */
        static const char *said[48];
        static int nsaid;
        int k = 0;
        while (k < nsaid && said[k] != xslow.tex)
            k++;
        if (k == nsaid && nsaid < 48) {
            said[nsaid++] = xslow.tex;
            printf("xbox slow draw: texture %s, %d triangles, arrays in store %d, predraw %d, env %d, scroll %d, "
                   "region %d, skin %d, own %d, stale %d\n", xslow.tex ? xslow.tex : "-", (int)count / 3, d->ok,
                   d->ok ? 0 : -1, xslow.env, xslow.scroll, xslow.rid, xslow.skin, xslow.own, xslow.stale);
        }
    }
#endif
    if (XSKIP(2048))
        return;
    if (d->sphere)
        sphere_coords(1);
    if (!d->arrays_set)
        xbox_client_arrays(d);
    if (d->baked) {                                  /* (gl4es has work for this texture: the part's own arrays,
                                                      * their coordinates unscaled) */
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        glScalef(d->rs[0], d->rs[1], 1);
        glMatrixMode(GL_MODELVIEW);
    }
    glDrawElements(GL_TRIANGLES, count, idx_type, idx);
    if (d->baked) {
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
    }
    if (d->sphere)
        sphere_coords(0);
}

/* part pi of a skinned scene mesh as the GPU poses it, made at its first draw; NULL: not this part (no normals,
 * a second layer or a texture matrix the program does not do, no room in the store), posed by the CPU instead */
static const SkinGpu *xbox_skin_gpu(Renderer *r, const SceneMesh *sm, const Skin *skin, uint32_t pi, uint32_t first,
                                    PartBuf *pb)
{
    if (pb->sg)
        return pb->sg->failed ? NULL : pb->sg;
    const Scene *s = r->scene;
    const MeshPart *p = &sm->mesh.parts[pi];
    SkinGpu *sg = calloc(1, sizeof *sg);
    if (!sg)
        return NULL;
    pb->sg = sg;
    float reg[4];
    int maxb = gliSkinMaxBones();
    if (maxb > SKIN_GROUP_BONES) maxb = SKIN_GROUP_BONES;
    uint32_t nv = p->nverts, nt = p->ntris;
    int32_t *vmap = NULL;
    uint32_t *gv = NULL;
    uint16_t *gi = NULL;
    if (maxb < 12 || !p->nrm || !p->pos || !p->tris || !nv || !nt || !skin->bones || !skin->weights ||
        first + nv > skin->nverts || (p->uv && material_region(sm->b, p->material, reg)) ||
        cube_material(s, sm, p->material) || material_env(sm->b, p->material) ||
        (p->col && p->uv && material_damage_layer(sm->b, p->material)) ||
        (s->cloud_material && sm->b == s->city && p->material == s->cloud_material))
        goto fail;
    vmap = malloc(sizeof *vmap * nv);
    gv = malloc(sizeof *gv * nv);
    gi = malloc(sizeof *gi * 3 * (size_t)nt);
    if (!vmap || !gv || !gi)
        goto fail;
    for (uint32_t t = 0; t < nt;) {
        uint8_t local[256], gb[SKIN_GROUP_BONES];
        int nb = 0;
        uint32_t ngv = 0, ngi = 0;
        memset(local, 0xFF, sizeof local);
        memset(vmap, 0xFF, sizeof *vmap * nv);
        for (; t < nt; t++) {                        /* the triangles this group's bones and vertices have room for */
            const uint32_t *tri = p->tris + 3 * t;
            uint8_t add[12];
            int nadd = 0;
            uint32_t fresh = 0;
            for (int c = 0; c < 3; c++) {
                uint32_t v = tri[c];
                if (v >= nv)
                    goto fail;
                if (vmap[v] < 0)
                    fresh++;
                for (int k = 0; k < 4; k++) {
                    if (!(skin->weights[first + v][k] > 0))
                        continue;
                    uint8_t b = skin->bones[first + v][k];
                    int known = local[b] != 0xFF;
                    for (int q = 0; q < nadd && !known; q++)
                        known = add[q] == b;
                    if (!known)
                        add[nadd++] = b;
                }
            }
            if (nb + nadd > maxb || ngv + fresh > SKIN_GROUP_VERTS || ngv + fresh > 65535u)
                break;
            for (int q = 0; q < nadd; q++) {
                local[add[q]] = (uint8_t)nb;
                gb[nb++] = add[q];
            }
            for (int c = 0; c < 3; c++) {
                uint32_t v = tri[c];
                if (vmap[v] < 0) {
                    vmap[v] = (int32_t)ngv;
                    gv[ngv++] = v;
                }
                gi[ngi++] = (uint16_t)vmap[v];
            }
        }
        if (!ngi)
            goto fail;
        SkinGroup *grown = realloc(sg->g, sizeof *grown * (size_t)(sg->ng + 1));
        if (!grown)
            goto fail;
        sg->g = grown;
        SkinGroup *g = &sg->g[sg->ng++];
        memset(g, 0, sizeof *g);
        g->pos = gliStaticArrayAlloc(sizeof(float) * 3 * ngv, sg);
        g->nrm = gliStaticArrayAlloc(sizeof(float) * 3 * ngv, sg);
        g->uv = p->uv ? gliStaticArrayAlloc(sizeof(float) * 2 * ngv, sg) : NULL;
        g->col = p->col ? gliStaticArrayAlloc(sizeof(float) * 4 * ngv, sg) : NULL;
        g->bone = gliStaticArrayAlloc(sizeof(int16_t) * 4 * ngv, sg);
        g->weight = gliStaticArrayAlloc(sizeof(float) * 4 * ngv, sg);
        g->idx = malloc(sizeof *g->idx * ngi);
        if (!g->pos || !g->nrm || (p->uv && !g->uv) || (p->col && !g->col) || !g->bone || !g->weight || !g->idx)
            goto fail;
        for (uint32_t q = 0; q < ngv; q++) {         /* (written once, in order: the store is write-combined) */
            uint32_t v = gv[q];
            g->pos[3 * q] = p->pos[3 * v]; g->pos[3 * q + 1] = p->pos[3 * v + 1]; g->pos[3 * q + 2] = p->pos[3 * v + 2];
        }
        for (uint32_t q = 0; q < ngv; q++) {
            uint32_t v = gv[q];
            g->nrm[3 * q] = p->nrm[3 * v]; g->nrm[3 * q + 1] = p->nrm[3 * v + 1]; g->nrm[3 * q + 2] = p->nrm[3 * v + 2];
        }
        for (uint32_t q = 0; g->uv && q < ngv; q++) {
            uint32_t v = gv[q];
            g->uv[2 * q] = p->uv[2 * v]; g->uv[2 * q + 1] = p->uv[2 * v + 1];
        }
        for (uint32_t q = 0; g->col && q < ngv; q++) {
            uint32_t v = gv[q];
            for (int c = 0; c < 4; c++)
                g->col[4 * q + c] = p->col[4 * v + c];
        }
        /* A vertex's bones in the order of their weights' places, the ones it has first: the two-bone program
         * reads the first two only, so a vertex whose two bones sit in places 2 and 3 would be lost to it. */
        int four = 0;
        for (uint32_t q = 0; q < ngv; q++) {
            uint32_t v = gv[q];
            int16_t out[4] = { 0, 0, 0, 0 };
            int n = 0;
            for (int k = 0; k < 4; k++)
                if (skin->weights[first + v][k] > 0)
                    out[n++] = (int16_t)(3 * local[skin->bones[first + v][k]]);
            if (n > 2)
                four = 1;
            g->bone[4 * q] = out[0]; g->bone[4 * q + 1] = out[1]; g->bone[4 * q + 2] = out[2]; g->bone[4 * q + 3] = out[3];
        }
        for (uint32_t q = 0; q < ngv; q++) {
            uint32_t v = gv[q];
            float out[4] = { 0, 0, 0, 0 };
            int n = 0;
            for (int k = 0; k < 4; k++)
                if (skin->weights[first + v][k] > 0)
                    out[n++] = skin->weights[first + v][k];
            g->weight[4 * q] = out[0]; g->weight[4 * q + 1] = out[1]; g->weight[4 * q + 2] = out[2]; g->weight[4 * q + 3] = out[3];
        }
        g->four = (uint8_t)four;
        memcpy(g->idx, gi, sizeof *g->idx * ngi);
        g->nverts = ngv;
        g->nidx = ngi;
        g->nb = (uint8_t)nb;
        memcpy(g->b, gb, (size_t)nb);
    }
    free(vmap); free(gv); free(gi);
    return sg;
fail:
    free(vmap); free(gv); free(gi);
    for (int k = 0; k < sg->ng; k++) {               /* (the record stays: not tried again for this part) */
        SkinGroup *g = &sg->g[k];
        if (g->pos) gliStaticArrayRemove(g->pos, sg);
        if (g->nrm) gliStaticArrayRemove(g->nrm, sg);
        if (g->uv) gliStaticArrayRemove(g->uv, sg);
        if (g->col) gliStaticArrayRemove(g->col, sg);
        if (g->bone) gliStaticArrayRemove(g->bone, sg);
        if (g->weight) gliStaticArrayRemove(g->weight, sg);
        free(g->idx);
    }
    free(sg->g);
    sg->g = NULL;
    sg->ng = 0;
    sg->failed = 1;
    return NULL;
}
#endif

static void draw_part(Renderer *r, const SceneMesh *sm, const Skin *skin, uint32_t i, uint32_t first, double t)
{
    pump_output(r);
    const Scene *s = r->scene;
    const MeshPart *p = &sm->mesh.parts[i];
    uint32_t tex = shown_texture(r, sm->b, material_texture(sm->b, p->material), t);
    float reg[4], rs[2] = { 1, 1 };                      /* a texture region: its tile (region_texture) */
    GLuint rid = p->uv && material_region(sm->b, p->material, reg) ? region_texture(r, sm->b, tex, reg, rs) : 0;
#ifdef RAMPAGE_XBOX
    /* A region's scale goes into the part's own copies when nothing else uses its coordinates unscaled (a damage or
     * reflection layer is drawn with them as they are) and its texture is one picture (not an .ifl): the taxis, the
     * trucks, the backdrops' bricks and windows are then drawn straight from GPU memory. */
    int bake = g_xbox_region_bake && rid && !skin && tex == material_texture(sm->b, p->material) &&
               !cube_material(s, sm, p->material) &&
               !(p->col && material_damage_layer(sm->b, p->material)) && !(p->nrm && material_env(sm->b, p->material)) &&
               !(s->cloud_material && sm->b == s->city && p->material == s->cloud_material);
    const PartBuf *xp = xbox_part(r, sm, i, skin != NULL, bake ? rs : NULL);
    XboxDirect xd;
    memset(&xd, 0, sizeof xd);
    xd.skin = skin;
    if (skin && skin->gpu_skin == 1 && !skin->cloth)
        ((Skin *)skin)->gpu_skin = xbox_skin_ok(r, sm, skin) ? 2 : -1;   /* (asked once, of all its parts) */
    if (skin && skin->gpu_skin > 0 && !skin->cloth) {   /* posed by the GPU */
        xd.sg = r->cur_inst && xp ? xbox_skin_gpu(r, sm, skin, i, first, (PartBuf *)xp) : NULL;
        if (!xd.sg) {                                /* no room in the store: the CPU poses it from the next frame on,
                                                      * and what was made for its other parts goes */
            xbox_skin_demote(r, sm, skin);
            return;
        }
    } else if (skin && !skin->pos)
        return;                                      /* (the CPU has not posed it yet) */
    int baked = bake && xp && xp->vbo == 1 && xp->own && xp->baked;   /* (drawn without the texture matrix) */
    if (baked) {
        xd.baked = 1;
        xd.rs[0] = rs[0];
        xd.rs[1] = rs[1];
    }
#else
    const int baked = 0;
#endif
    GLuint id = rid ? rid : texture(r, sm->b, tex);
    int cube = cube_material(s, sm, p->material);
    GLuint cube_env = cube ? texture(r, s->city, s->env_map) : 0;
    /* BUILDING_CUBEMAP, shader 0x1B (record 27 at 0x3B5CC8) [R]: one pass, texture slot 0 (the EnvMap, in place of its
     * own Concrete_Grey) at the sphere-map coordinates, TFX 1 DECAL with TCC 0 and colour mode 0 (the texture's colour
     * as it is: unlit), GS state 1 (opaque, no alpha test, fogged). The fountain sculpture of NY05, CH and the others */
    int env_only = cube == 1 && cube_env && (p->nrm || skin);
    if (env_only) {
        id = cube_env;
        rid = 0;
    }
#ifdef RAMPAGE_XBOX
    xs_enable(&xs.cull, GL_CULL_FACE, !(material_flags(sm->b, p->material) & MAT_TWO_SIDED));
    if (id) {
        xs_bind(id);
        xs_wrap(id, !rid && !env_only && material_clamped(sm->b, p->material) ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    } else
        xs_enable(&xs.tex2d, GL_TEXTURE_2D, 0);
#else
    if (material_flags(sm->b, p->material) & MAT_TWO_SIDED) glDisable(GL_CULL_FACE);
    else glEnable(GL_CULL_FACE);
    if (id) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, id);
        /* its wrap [R 0x265840 -> 0x26AA20, set for each pass]: clamped by its material (material_clamped: the
         * skylines, fog masks, backdrops, roofs, which do not tile; repeated, their bilinear taps took in the
         * picture's other edge, the thin light line along SF10's skyline), else repeated; a region's tile and the
         * EnvMap repeat */
        GLint wrap = !rid && !env_only && material_clamped(sm->b, p->material) ? GL_CLAMP_TO_EDGE : GL_REPEAT;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    } else {
        glDisable(GL_TEXTURE_2D);
    }
#endif
    int scroll = s->cloud_material && sm->b == s->city && p->material == s->cloud_material;
    float drift[2] = { 0, 0 };
    if (scroll) {
        drift[0] = (float)fmod(s->cloud_rate[0] * t, 1.0);
        drift[1] = (float)fmod(s->cloud_rate[1] * t, 1.0);
    }
    if (scroll || (rid && !baked)) {                     /* the drifting cloud band (AnimatingSkyData); the tile's
                                                          * scale, after the drift as the GS wraps the UVs it is sent */
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        if (rid)
            glScalef(rs[0], rs[1], 1);
        if (scroll)
            glTranslatef(drift[0], drift[1], 0);
        glMatrixMode(GL_MODELVIEW);
    }
    /* port extra: an unskinned part from its GL buffers (part_buffers: the pointers are their offsets), a skinned one
     * (posed every frame) from the client arrays */
    const PartBuf *pb = skin ? NULL : part_buffers(r, sm, i);
    const void *pos = p->pos, *nrm = p->nrm, *uv = p->uv, *col = p->col, *idx = p->tris;
#ifdef RAMPAGE_XBOX
    int xbox_static = (xp && xp->vbo == 1 && !xp->own) || (skin && skin->gpu);   /* (a posed mesh in the GPU's store;
                                                      * an unposed part's own copies are not found by its addresses) */
    if (xbox_static)
        gliStaticArraysHint(1);                      /* this part's draws, to the end of the function */
    GLenum idx_type = GL_UNSIGNED_INT;
    if (xp && xp->idx16) {
        xd.idx16 = xp->idx16;
        xd.nidx16 = xp->own ? xp->nidx16 : 0;
        if (!xp->stale) {                            /* (a changed part's old copies: their indices are not the
                                                      * part's; a draw from the client arrays takes the part's own) */
            idx = xp->idx16;
            idx_type = GL_UNSIGNED_SHORT;
        }
    }
    (void)pos; (void)nrm; (void)uv; (void)col;
    xd.cpos = skin ? (xd.sg ? NULL : skin->pos + 3 * first) : p->pos;
    xd.cnrm = skin ? (xd.sg ? NULL : skin->nrm + 3 * first) : p->nrm;
    xd.cuv = p->uv;
    xd.ccol = p->col;
    if (!xd.sg && xd.idx16) {                        /* its arrays in GPU memory, when all of them are */
        int reg = xp && xp->vbo == 1;
        xd.pos = skin ? (skin->gpu ? skin->pos + 3 * first : NULL) : reg ? xp->gpos : NULL;
        xd.nrm = skin ? (skin->gpu ? skin->nrm + 3 * first : NULL) : reg ? xp->gnrm : NULL;
        xd.uv = reg ? xp->guv : NULL;
        xd.col = reg ? xp->gcol : NULL;
        xd.ok = xd.pos && (!xd.cnrm || xd.nrm) && (!p->uv || xd.uv) && (!p->col || xd.col);
    }
    if (!xd.ok && !xd.sg)
        xbox_client_arrays(&xd);
    if (!xd.cnrm && !xd.sg)
        glNormal3f(0, 0, 1);
#else
    const GLenum idx_type = GL_UNSIGNED_INT;
    if (pb) {
        g_buf.bind(GL_ARRAY_BUFFER, pb->vbo);
        g_buf.bind(GL_ELEMENT_ARRAY_BUFFER, pb->ibo);
        pos = idx = NULL;
        nrm = (const void *)(uintptr_t)pb->at_nrm;
        uv = (const void *)(uintptr_t)pb->at_uv;
        col = (const void *)(uintptr_t)pb->at_col;
    }
    glVertexPointer(3, GL_FLOAT, 0, skin ? skin->pos + 3 * first : pos);
    if (skin) { glEnableClientState(GL_NORMAL_ARRAY); glNormalPointer(GL_FLOAT, 0, skin->nrm + 3 * first); }
    else if (p->nrm) { glEnableClientState(GL_NORMAL_ARRAY); glNormalPointer(GL_FLOAT, 0, nrm); }
    else { glDisableClientState(GL_NORMAL_ARRAY); glNormal3f(0, 0, 1); }
    if (p->uv) { glEnableClientState(GL_TEXTURE_COORD_ARRAY); glTexCoordPointer(2, GL_FLOAT, 0, uv); }
    else glDisableClientState(GL_TEXTURE_COORD_ARRAY);
#endif
    float mc[4];                                         /* the material's colour (white when textured) */
    material_color(sm->b, p->material, mc);
#ifdef RAMPAGE_XBOX
    if (!p->col)
        glColor4fv(mc);                              /* (the colour array, when there is one, with the others) */
#else
    if (p->col) { glEnableClientState(GL_COLOR_ARRAY); glColorPointer(4, GL_FLOAT, 0, col); }
    else { glDisableClientState(GL_COLOR_ARRAY); glColor4fv(mc); }
#endif
    /* the part's own blend (the facades'), else its material's: GS state 2 as the sheets' (material_blended) */
    int blend = p->blend ? p->blend : material_blended(sm->b, p->material) ? 2 : 0;
    int see_through = !blend && mc[3] < 0.98f;           /* the glass of the saucer, the hover tank, the
                                                          * helicopter's canopy: blended by the colour's alpha (kind
                                                          * 1 with flag 0x100000, shaders 0xE / 0x10: GS state 7) */
    /* colours and no normals: the light is in the colours (docs/BUILDING_LIGHT_NOTES.md), drawn unlit: the
     * damaged interiors (their sections are format 0x142, prelit, like the game's own 0x142 buffers of shader
     * 0x51), the facades and cracks the building light colours, the ground and top models lit with it. No normals
     * and no colours (format 0x102: the Las Vegas signs, CASINO, Gold Rush, the billboards' marquees) is unlit as
     * well: the texture at full strength, by night as by day [G: nothing for the lights to work on; the game's own
     * night shots show them so, not tinted by the night's light] */
#ifdef RAMPAGE_XBOX
    int prelit = !p->nrm && !skin && xs_light_base();
    xs_light(prelit ? 0 : xs_light_base());          /* (left so: the next part, or the end of the run, changes it) */
#else
    int prelit = !p->nrm && !skin && glIsEnabled(GL_LIGHTING);
    if (prelit)
        glDisable(GL_LIGHTING);
#endif
    /* a damage layer (material_damage_layer, shader 0x7A): the vertex alpha is the layer's share, not see-through:
     * the part itself is drawn opaque, without the alpha test */
    uint32_t dmg = p->col && p->uv ? material_damage_layer(sm->b, p->material) : 0;
    GLboolean dmg_test = dmg ? glIsEnabled(GL_ALPHA_TEST) : GL_FALSE;
    if (dmg)
        glDisable(GL_ALPHA_TEST);
    if (blend || see_through) {                          /* over what is behind, by its alpha */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        /* a monster skin in state 2 cuts its see-through texels at half [G: the skinned path's own TEST (0x1AB270 ->
         * 0x259E50 -> 0x261FA0) not traced; kept at alpha > 0, the fur fringes of Rojo, Gilman, Boosy and Leon
         * (palette alpha 4/128 inside, 17 .. 115 at the edge) wrote their depth midway through the mesh and hid the
         * legs drawn after them: the street showed through Rojo's loincloth and hocks, issue #6] */
        glAlphaFunc(GL_GREATER, blend == 2 && skin ? 0.5f : blend == 2 || see_through ? 0.0f : 0.01f);
        if (blend != 2 && !see_through)                  /* a facade sheet and the glass keep writing their depth [R
                                                          * GS states 2 and 7: ALPHA 0x44, TEST 0x5000D (alpha > 0),
                                                          * Z written] */
            glDepthMask(GL_FALSE);
        if (blend == 1)                                  /* the cracks: Z test GEQUAL, the GS's z growing nearer [R
                                                          * shader 0x4C, GS state 2], so the ground floor's, on the
                                                          * very triangles of its model (city.c ground_cracks_make),
                                                          * show on them */
            glDepthFunc(GL_LEQUAL);
    }
    /* a reflective material (kind 5: shader 0x1A, record 26 at 0x3B5C10; 0x19 with flag 2, set on none) [R]: its first
     * pass is GS state 1, TEST 0x50002, no alpha test: the texture's alpha is not a cut-out but the share of the
     * reflection (written into the frame's alpha for the second pass; the blimp's envelope, not its lettering or
     * gondola). So it is drawn whole */
    uint32_t layer = p->nrm || skin ? material_env(sm->b, p->material) : 0;
    /* and any opaque material's (material_uncut: GS states 1 and 5, TEST 0x50002) [R]: drawn whole, its texture's or
     * vertices' alpha cutting nothing (Chicago's Adornwindow panes at alpha 0, the rubble beams' foot) */
    int uncut = layer || env_only || material_uncut(sm->b, p->material);
    GLboolean test_was = uncut && !blend && !see_through ? glIsEnabled(GL_ALPHA_TEST) : GL_FALSE;
    if (test_was)
        glDisable(GL_ALPHA_TEST);
#ifdef RAMPAGE_XBOX
    if (env_only) {
        xd.sphere = 1;                               /* (xbox_part_draw: by the GPU, or sphere_coords) */
        xs_light(0);
        xd.col = xd.ccol = NULL;
        if (xd.arrays_set)
            xs_client(&xs.col, GL_COLOR_ARRAY, 0);
        glColor4f(1, 1, 1, 1);
    }
#else
    GLboolean lit_was = env_only ? glIsEnabled(GL_LIGHTING) : GL_FALSE;
    if (env_only) {
        sphere_coords(1);
        glDisable(GL_LIGHTING);
        glDisableClientState(GL_COLOR_ARRAY);
        glColor4f(1, 1, 1, 1);
    }
#endif
#ifdef RAMPAGE_XBOX
#ifdef RAMPAGE_XBOX_TEST_ARGS
    xslow.tex = bdp_name(sm->b, tex);
    xslow.env = env_only + 2 * (layer != 0) + 4 * (cube != 0);
    xslow.scroll = scroll;
    xslow.rid = rid ? 1 + baked : 0;
    xslow.skin = skin != NULL;
    xslow.own = xp ? xp->own : -1;
    xslow.stale = xp ? xp->stale + 2 * (xp->vbo == 1) : -1;
#endif
    xbox_part_draw(r, &xd, (GLsizei)(3 * p->ntris), idx_type, idx);
#else
    glDrawElements(GL_TRIANGLES, (GLsizei)(3 * p->ntris), idx_type, idx);
#endif
    if (env_only) {
#ifdef RAMPAGE_XBOX
        xd.sphere = 0;
#else
        sphere_coords(0);
        if (lit_was)
            glEnable(GL_LIGHTING);
#endif
    }
    if (test_was)
        glEnable(GL_ALPHA_TEST);
    if (rid && !baked) {                                 /* the region is texture slot 0's alone (0x265840) */
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        if (scroll)
            glTranslatef(drift[0], drift[1], 0);
        glMatrixMode(GL_MODELVIEW);
    }
    if (blend || see_through) {
        glDisable(GL_BLEND);
        glAlphaFunc(GL_GREATER, 0.5f);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
    }
    if (dmg) {                                           /* the second pass: the layer over the part, (Cs - Cd) x Ad + Cd,
                                                          * Ad the alpha of the first: its opaque texture's x the vertex's */
        GLuint id2 = texture(r, sm->b, dmg);
        if (id2) {
            glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_TEXTURE_BIT);
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, id2);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_ALPHA_TEST);                 /* a share of 0 adds nothing */
            glAlphaFunc(GL_GREATER, 0.0f);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);
#ifdef RAMPAGE_XBOX
            xbox_part_draw(r, &xd, (GLsizei)(3 * p->ntris), idx_type, idx);
#else
            glDrawElements(GL_TRIANGLES, (GLsizei)(3 * p->ntris), idx_type, idx);
#endif
            glPopAttrib();
            r->drawn += p->ntris;
        }
        if (dmg_test)
            glEnable(GL_ALPHA_TEST);
    }
    r->drawn += p->ntris;
    /* the reflection layer, shader 0x1A's second pass [R]: texture slot 1 (the material's +0x3A `cubemap`, or for an
     * RCUBEMAP* material the block's EnvMap, 0x2B96E4) at the sphere-map coordinates (VU1 0x34F1C8, sphere_coords),
     * TFX 1 DECAL, TCC 0, colour mode 0 (the texture's colour, unlit), GS state 10: ALPHA 0x58 = Cs x Ad + Cd (added,
     * by the frame's alpha, which the first pass left: the texture's alpha x the vertex's, 0x80 = 1), PRMODE 0x58 (not
     * fogged), Z tested, not written. The frame's alpha is GL_DST_ALPHA here (1 where the frame has none: then the
     * whole reflection is added) */
    GLuint env = layer ? (cube == 2 && cube_env ? cube_env : texture(r, sm->b, layer)) : 0;
    if (env) {
        glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_TEXTURE_BIT | GL_LIGHTING_BIT);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, env);
#ifndef RAMPAGE_XBOX
        sphere_coords(1);
#endif
        glDisable(GL_LIGHTING);
        glDisable(GL_FOG);
        glDisable(GL_ALPHA_TEST);
#ifdef RAMPAGE_XBOX
        xd.sphere = 1;
        xd.col = xd.ccol = NULL;
        if (xd.arrays_set)
            xs_client(&xs.col, GL_COLOR_ARRAY, 0);
#else
        glDisableClientState(GL_COLOR_ARRAY);
#endif
        glColor4f(1, 1, 1, 1);
        glEnable(GL_BLEND);
        glBlendFunc(GL_DST_ALPHA, GL_ONE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
#ifdef RAMPAGE_XBOX
        xbox_part_draw(r, &xd, (GLsizei)(3 * p->ntris), idx_type, idx);
        xd.sphere = 0;
#else
        glDrawElements(GL_TRIANGLES, (GLsizei)(3 * p->ntris), idx_type, idx);
        sphere_coords(0);
#endif
        glPopAttrib();
    }
#ifndef RAMPAGE_XBOX
    if (prelit)
        glEnable(GL_LIGHTING);
#endif
    if (scroll) {
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
    }
    if (pb) {                                            /* the other draws' client arrays */
        g_buf.bind(GL_ARRAY_BUFFER, 0);
        g_buf.bind(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
#ifdef RAMPAGE_XBOX
    if (xbox_static)
        gliStaticArraysHint(0);
#endif
}

/* the particles (fx.c): after the world, not writing depth, tested against it unless the effect says not to
 * (-TestZBuffer); additive or alpha blended; the colour times the texture (the game's 128 = 1) */
/* the water surfaces (water.c; ROWater 0x339D00): the EnvMap texture at the view's reflection, opaque, fogged as the
 * world. Shader 0x7E's pass [R]: TCC 0, TFX 1 (DECAL: the texture's colour, the vertex alpha), no alpha test (TEST
 * 0x50002), texture flags 0x11 (bit 0: CLAMP_1 REPEAT on S and T, 0x26AA20) */
static void draw_water(Renderer *r, const Scene *s, const Camera *cam)
{
#ifdef RAMPAGE_XBOX
    if (XSKIP(256))
        return;
#endif
    XS_END();
    float *uv = r->water_uv;
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    GLboolean atest = glIsEnabled(GL_ALPHA_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glColor4f(1, 1, 1, 1);
    for (int k = 0; k < s->nwater; k++) {
        const Water *w = s->water[k];
        int nv = water_vertex_count(w), ntris = 0;
        if (nv <= 0 || (size_t)nv > SIZE_MAX / (2 * sizeof(float)))
            continue;
        if ((size_t)nv > r->nwater_uv) {
            float *grown = realloc(r->water_uv, sizeof(float) * 2 * (size_t)nv);
            if (!grown)
                continue;
            r->water_uv = uv = grown;
            r->nwater_uv = (size_t)nv;
        }
        water_env_uv(w, cam->pos, uv);
        int name = water_texture(w) ? bdp_find_name(s->city, water_texture(w)) : -1;
        GLuint id = name > 0 ? texture(r, s->city, (uint32_t)name) : 0;
        if (id) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, id);
            XS_TEXTURE_BOUND();
        } else
            glDisable(GL_TEXTURE_2D);
        const uint32_t *tris = water_triangles(w, &ntris);
        glVertexPointer(3, GL_FLOAT, 0, water_positions(w));
        glTexCoordPointer(2, GL_FLOAT, 0, uv);
        glDrawElements(GL_TRIANGLES, 3 * ntris, GL_UNSIGNED_INT, tris);
    }
    if (atest)
        glEnable(GL_ALPHA_TEST);
    glEnable(GL_LIGHTING);
}

/* the HUD's quads (hud.c): the GUI's 640 x 480 screen at the window's height, centred (its 4:3 kept), in order, no
 * depth, alpha blended */
static void draw_fx(Renderer *r, const FxQuad *fx, int nfx, const Camera *cam, float fog_start, float fog_end, int world);

static void hud_state(float aspect)
{
    float half = HUD_SCREEN_H / 2 * aspect;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(HUD_SCREEN_W / 2 - half, HUD_SCREEN_W / 2 + half, HUD_SCREEN_H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

/* a 3D layer of the front end through the GUI camera (shell.h): eye (0, -1, 0) looking along +y, z up, a vertical field
 * of view of 2 atan(0.375), near 1, far 10000 [R 0x227AD0]; its models lit by their light set, its particles over them
 * (the 2D quads write no depth: the globe at depth 10 shows over CitySelect's background at depth 1, drawn before it) */
static void draw_gui3d(Renderer *r, const SceneGui3D *l, float aspect, double t)
{
    Camera cam = { { 0, -1, 0 }, 1.5707963f, 0, 41.1120914f, 0, { { 0 } } };
    perspective(cam.fov_deg, aspect, 1, 10000);
    look(&cam);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    glColor4f(1, 1, 1, 1);
    if (l->nmeshes) {
        if (l->lit) {
            glEnable(GL_LIGHTING);
            float amb[4] = { l->ambient[0], l->ambient[1], l->ambient[2], 1 };
            glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
            for (int i = 0; i < 4; i++) {
                if (i >= l->nlights) {
                    glDisable(GL_LIGHT0 + i);
                    continue;
                }
                const float *d = l->light_dir[i];
                float to_light[4] = { -d[0], -d[1], -d[2], 0 }, zero[4] = { 0, 0, 0, 1 };
                float dif[4] = { l->light_diffuse[i][0], l->light_diffuse[i][1], l->light_diffuse[i][2], 1 };
                float la[4] = { l->light_ambient[i][0], l->light_ambient[i][1], l->light_ambient[i][2], 1 };
                glLightfv(GL_LIGHT0 + i, GL_POSITION, to_light);
                glLightfv(GL_LIGHT0 + i, GL_DIFFUSE, dif);
                glLightfv(GL_LIGHT0 + i, GL_AMBIENT, la);
                glLightfv(GL_LIGHT0 + i, GL_SPECULAR, zero);
                glEnable(GL_LIGHT0 + i);
            }
        } else
            glDisable(GL_LIGHTING);
        glEnableClientState(GL_VERTEX_ARRAY);
        for (int i = 0; i < l->nmeshes; i++) {
            glPushMatrix();
            glMultMatrixf(l->meshes[i].world.m);
            draw_mesh(r, l->meshes[i].mesh, NULL, t);
            glPopMatrix();
        }
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisable(GL_LIGHTING);
    }
    glDisable(GL_CULL_FACE);
    if (l->nfx)
        draw_fx(r, l->fx, l->nfx, &cam, 0, 0, 0);
    hud_state(aspect);
}

void rnd_hud_time(Renderer *r, double t)
{
    if (r) {
        r->hud_t = t;
        r->has_hud_t = 1;
    }
}

#ifdef RAMPAGE_XBOX
/* Reject only quads wholly beyond one viewport edge. All four submitted
 * corners matter: glyph skew and rotated cards may cross an edge even when
 * their unmodified rectangle does not. Leave a reference-pixel safety margin. */
static int hud_quad_visible(const HudQuad *q, float aspect)
{
    float left = HUD_SCREEN_W * 0.5f - HUD_SCREEN_H * 0.5f * aspect;
    float right = HUD_SCREEN_W * 0.5f + HUD_SCREEN_H * 0.5f * aspect;
    float xmin = INFINITY, xmax = -INFINITY, ymin = INFINITY, ymax = -INFINITY;
    for (int k = 0; k < 4; ++k) {
        float x = q->corners ? q->cx[k] : ((k == 0 || k == 3) ? q->x0 : q->x1) + (k < 2 ? q->skew : 0);
        float y = q->corners ? q->cy[k] : (k < 2 ? q->y0 : q->y1);
        if (!isfinite(x) || !isfinite(y)) return 1;
        xmin = fminf(xmin, x); xmax = fmaxf(xmax, x);
        ymin = fminf(ymin, y); ymax = fmaxf(ymax, y);
    }
    return !(xmax < left - 1 || xmin > right + 1 || ymax < -1 || ymin > HUD_SCREEN_H + 1);
}
#endif

static void draw_hud(Renderer *r, const Scene *s, int width, int height, double t)
{
    XS_END();
    float aspect = height ? (float)width / height : 4.0f / 3;
    if (r->has_hud_t)                                /* the GUI's own clock: it runs on while the world is paused */
        t = r->hud_t;
    hud_state(aspect);
    int cut = (int)(height * 4.0f / 3 + 0.5f);       /* the 640 x 480 screen's width in pixels */
    int clip = s->hud_clip && cut < width;           /* nothing past its edges: the PS2's frame ends there */
    if (clip)
#ifdef RAMPAGE_XBOX
        glScissor((width - cut) / 2 * xbox_pixel_width() / width, 0, cut * xbox_pixel_width() / width, xbox_pixel_height());
#else
        glScissor((width - cut) / 2, 0, cut, height);
#endif
    if (s->ngui3d)
        glClear(GL_DEPTH_BUFFER_BIT);
    r->in_hud = 1;
    int layer = 0;
#ifdef RAMPAGE_XBOX
    GLuint qb_id = ~0u;
    int qb_scissor = -1, qb_add = -1;
    qb_begin();
#endif
    for (int i = 0; i < s->nhud; i++) {
        const HudQuad *q = &s->hud[i];
#ifdef RAMPAGE_XBOX
        if (!q->b && q->tex == HUD_TEX_3D) {
            qb_end();                                /* a 3D layer sets its own state: the quads after it start anew */
            glDisable(GL_SCISSOR_TEST);
            if (layer < s->ngui3d)
                draw_gui3d(r, &s->gui3d[layer], aspect, t);
            layer++;
            qb_id = ~0u;
            qb_scissor = qb_add = -1;
            qb_begin();
            continue;
        }
#else
        if (clip && !q->wide)                        /* (the widened fills go across the window) */
            glEnable(GL_SCISSOR_TEST);
        else
            glDisable(GL_SCISSOR_TEST);
        if (!q->b && q->tex == HUD_TEX_3D) {
            if (layer < s->ngui3d)
                draw_gui3d(r, &s->gui3d[layer], aspect, t);
            layer++;
            continue;
        }
#endif
#ifdef RAMPAGE_XBOX
        /* Cull before texture lookup/upload, but never skip a 3D-layer sentinel. */
        if (!hud_quad_visible(q, aspect)) continue;
#endif
        r->in_hud_quad = 1;
        GLuint id = q->b ? texture(r, q->b, q->tex) : q->tex == HUD_TEX_VIDEO ? video_texture(r) : 0;
        r->in_hud_quad = 0;
        if (q->b && !id)
            continue;
        static const float black[4] = { 0, 0, 0, 1 };
        const float *col = !q->b && q->tex == HUD_TEX_VIDEO && !id ? black : q->col;   /* no picture: black */
#ifdef RAMPAGE_XBOX
        {                                            /* the state as below, set only when it changes; the quad to
                                                      * the batch, its colour at its corners */
            int scissor = clip && !q->wide, add = q->additive != 0;
            if (id != qb_id || scissor != qb_scissor || add != qb_add) {
                qb_flush();
                if (scissor != qb_scissor) { if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST); }
                if (id != qb_id) {
                    if (id) {
                        glEnable(GL_TEXTURE_2D);
                        glBindTexture(GL_TEXTURE_2D, id);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    } else
                        glDisable(GL_TEXTURE_2D);
                }
                if (add != qb_add) glBlendFunc(GL_SRC_ALPHA, add ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
                qb_id = id; qb_scissor = scissor; qb_add = add;
            }
            if (q->corners)
                for (int k = 0; k < 4; k++)
                    qb_vertex(k, q->cx[k], q->cy[k], 0, q->cu[k], q->cv[k], col, col[3]);
            else {
                qb_vertex(0, q->x0 + q->skew, q->y0, 0, q->u0, q->v0, col, col[3]);
                qb_vertex(1, q->x1 + q->skew, q->y0, 0, q->u1, q->v0, col, col[3]);
                qb_vertex(2, q->x1, q->y1, 0, q->u1, q->v1, col, col[3]);
                qb_vertex(3, q->x0, q->y1, 0, q->u0, q->v1, col, col[3]);
            }
            qb_next();
            continue;
        }
#endif
        if (id) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, id);
            XS_TEXTURE_BOUND();
            /* clamped [no PS2 meaning]: the GUI's shaders repeat as the textures load (their compiled records'
             * wrap 0x11, FORMAT_GUI.md "GuiQuad"), but on the PS2 a picture is drawn at about a texel a pixel,
             * where the edge pixels sample the edge texels; in a bigger window the outer pixels' bilinear taps
             * reached the picture's other edge (the thin lines along the city cards' frame and across them, the
             * frost tiles' seams). Clamped, the edges are as the PS2's screen shows them. The world's draws repeat */
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        } else
            glDisable(GL_TEXTURE_2D);
        glColor4fv(col);
        glBlendFunc(GL_SRC_ALPHA, q->additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
        glBegin(GL_QUADS);
        if (q->corners)
            for (int k = 0; k < 4; k++) {
                glTexCoord2f(q->cu[k], q->cv[k]);
                glVertex2f(q->cx[k], q->cy[k]);
            }
        else {
            glTexCoord2f(q->u0, q->v0); glVertex2f(q->x0 + q->skew, q->y0);
            glTexCoord2f(q->u1, q->v0); glVertex2f(q->x1 + q->skew, q->y0);
            glTexCoord2f(q->u1, q->v1); glVertex2f(q->x1, q->y1);
            glTexCoord2f(q->u0, q->v1); glVertex2f(q->x0, q->y1);
        }
        glEnd();
#ifndef RAMPAGE_XBOX
        if (id) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        }
#endif
    }
#ifdef RAMPAGE_XBOX
    qb_end();
    XS_TEXTURE_BOUND();
#endif
    r->in_hud = 0;
    glDisable(GL_SCISSOR_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    glEnable(GL_ALPHA_TEST);
    glEnable(GL_DEPTH_TEST);
    glColor4f(1, 1, 1, 1);
}

/* the shadows of one pass (shadow.h): the ground grid with no depth test right after the street; the decals and the
 * wall blob over the world, depth tested and pulled in front of what they lie on (the wall blob has no depth test in
 * the game, drawn before the monster; here the monster is already drawn) */
static void draw_shadows(Renderer *r, int pass)
{
#ifdef RAMPAGE_XBOX
    if (XSKIP(128))
        return;
#endif
    XS_END();
    const Scene *s = r->scene;
    const ShadowList *l = s->shadows;
    int first = 0;
    while (first < l->nb && l->b[first].pass != pass)
        first++;
    if (first == l->nb)
        return;                                      /* no draws: no state stack or its temporary allocations */
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT | GL_CURRENT_BIT |
                 GL_TEXTURE_BIT | GL_LIGHTING_BIT);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glShadeModel(GL_SMOOTH);
    for (int i = first; i < l->nb; i++) {
        const ShadowBatch *b = &l->b[i];
        if (b->pass != pass)
            continue;
        int name = b->texture && s->shadow_bundle ? bdp_find_name(s->shadow_bundle, b->texture) : -1;
        GLuint id = name >= 0 ? texture(r, s->shadow_bundle, (uint32_t)name) : 0;
        if (b->texture && !id)
            continue;
        if (id) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, id);
            XS_TEXTURE_BOUND();
        } else
            glDisable(GL_TEXTURE_2D);
        glAlphaFunc(GL_GREATER, b->alpha_test ? 1.0f / 255 : 0.0f);
        if (pass == SHADOW_PASS_GROUND) {
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_POLYGON_OFFSET_FILL);
        } else {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1, -1);
        }
#ifdef RAMPAGE_XBOX
        if (b->count > 0) {                          /* the same vertices, as arrays in place of a call for each */
            const ShadowVert *v = &l->v[b->first];
            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glEnableClientState(GL_COLOR_ARRAY);
            glVertexPointer(3, GL_FLOAT, sizeof *v, v->pos);
            glTexCoordPointer(2, GL_FLOAT, sizeof *v, v->uv);
            glColorPointer(4, GL_FLOAT, sizeof *v, v->rgba);
            glDrawArrays(GL_TRIANGLES, 0, b->count);
        }
#else
        glBegin(GL_TRIANGLES);
        for (int k = b->first; k < b->first + b->count; k++) {
            const ShadowVert *v = &l->v[k];
            glColor4fv(v->rgba);
            glTexCoord2fv(v->uv);
            glVertex3fv(v->pos);
        }
        glEnd();
#endif
    }
    glPopClientAttrib();
    glPopAttrib();
}

/* the particles (fx.c): the game's VU1 program for them (0x3A75C8 .. 0x3A7D78, shader 0x3B2660, vertex format 0x412)
 * takes the corners the CPU made (0x2214D0), projects them, and multiplies each vertex's alpha by the world's fog
 * factor: min(127.5, F / 2) / 128, F the 0 .. 255 the world's programs send the GS, which draws them unfogged (PRMODE
 * 0x58: no FGE) [R]. So a fogged particle fades out rather than taking the fog colour: here the linear fog's factor at
 * each corner's eye-plane depth times its alpha, GL fog off. The GS state (0x3B26C0 additive, 0x3B2700 blended): ALPHA
 * 0x48 (Cs + Cd x As) or 0x44, TEST 0x51001 (depth test GEQUAL, the alpha test failing every pixel into the frame
 * buffer only: no depth written), the depth test switched off for -TestZBuffer (0x25A900) [R]. In the world such a
 * particle is still hidden by what stands in front of it (a car's explosion behind a building), as the game's: 90 of
 * the 287 effects have one, and none shows through the buildings there [G: the game's effects drawn in the world's
 * order, the draw not traced]. Here it is tested at its depth pulled towards the eye by its size, so what it starts
 * in (the car, the street under it) does not cut it */
#ifdef RAMPAGE_XBOX
/* draw_fx's thinning (g_xbox_fx_budget). Two quads that are one particle drawn again (fx.c: int.multiple_render). */
static inline int fx_same(const FxQuad *a, const FxQuad *b)
{
    return a->tex == b->tex && a->b == b->b && a->additive == b->additive && a->ztest == b->ztest &&
           memcmp(a, b, offsetof(FxQuad, b)) == 0;
}

/* The share of the screen a quad covers, near enough: its box about its middle, cut at the screen's edges. M the
 * view matrix, px and py the projection's two scales. */
static float fx_share(const FxQuad *q, const float *M, float px, float py)
{
    float ex = M[0] * q->pos[0] + M[4] * q->pos[1] + M[8] * q->pos[2] + M[12];
    float ey = M[1] * q->pos[0] + M[5] * q->pos[1] + M[9] * q->pos[2] + M[13];
    float ez = -(M[2] * q->pos[0] + M[6] * q->pos[1] + M[10] * q->pos[2] + M[14]);
    float hw, hh, fill = 1;                                /* half the box in the camera's plane; how much of it is quad */
    if (q->kind == FXQ_BILLBOARD) {
        if (ez < 0.1f)
            return 0;                                      /* (behind the camera: all of it) */
        hw = q->size[0] * 0.5f;
        hh = q->size[1] * 0.5f;
    } else {
        const float *a = q->axis[0], *b = q->axis[1];
        float ax = fabsf(M[0] * a[0] + M[4] * a[1] + M[8] * a[2]), ay = fabsf(M[1] * a[0] + M[5] * a[1] + M[9] * a[2]);
        if (q->kind == FXQ_FLAT) {
            hw = ax + fabsf(M[0] * b[0] + M[4] * b[1] + M[8] * b[2]);
            hh = ay + fabsf(M[1] * b[0] + M[5] * b[1] + M[9] * b[2]);
        } else {                                           /* a streak: its length by its width, in a box that holds it */
            float w = q->size[1] * 0.5f, len = sqrtf(ax * ax + ay * ay);
            hw = ax + w;
            hh = ay + w;
            fill = hw * hh > 0 ? (len + w) * w / (hw * hh) : 0;
            if (fill > 1)
                fill = 1;
        }
        if (ez < 0.1f)
            ez = 0.1f;                                     /* (through the camera's plane: as large as the screen lets it) */
    }
    float k = 1 / ez, cx = px * ex * k, cy = py * ey * k, sw = px * hw * k, sh = py * hh * k;
    float x0 = cx - sw, x1 = cx + sw, y0 = cy - sh, y1 = cy + sh;
    if (x0 < -1) x0 = -1;
    if (x1 > 1) x1 = 1;
    if (y0 < -1) y0 = -1;
    if (y1 > 1) y1 = 1;
    return x1 > x0 && y1 > y0 ? (x1 - x0) * (y1 - y0) * 0.25f * fill : 0;
}

/* 1 - (1 - a)^n, n >= 1: the alpha of one quad that hides what n quads of alpha a over each other hide (between
 * whole n, the straight line). */
static inline float fx_denser(float a, float n)
{
    if (a >= 1)
        return 1;
    if (a <= 0)
        return 0;
    if (n > 64)
        n = 64;
    int whole = (int)n;
    float left = 1 - a, p = left;
    for (int i = 1; i < whole; i++)
        p *= left;
    return 1 - p * (1 - (n - (float)whole) * a);
}
#endif

#if defined RAMPAGE_XBOX && defined RAMPAGE_XBOX_TEST_ARGS
/* test builds: how much of the picture the effects' quads cover (their overdraw is what a console's GPU pays for at
 * 1280 x 720), by texture, for main.c's line every 5 s. In screens: 1.0 is every pixel blended once. */
typedef struct { uint32_t tex; int add; float px; unsigned n; } FxStatTex;
static struct {
    unsigned frames, quads, faint, near, merged;
    float px, big, peak, asked, keep, budget, rest;
    FxStatTex t[24];
} fxs;
void rnd_fx_stats(char *out, size_t cap)
{
    float f = fxs.frames ? 1.0f / fxs.frames : 0;
    int n = snprintf(out, cap, "quads=%.0f screens=%.2f big=%.2f peak=%.2f faint=%.0f near=%.1f asked=%.2f keep=%.2f "
                     "merged=%.0f budget=%.2f rest_ms=%.1f", fxs.quads * f, fxs.px * f, fxs.big * f, fxs.peak,
                     fxs.faint * f, fxs.near * f, fxs.asked * f, fxs.keep * f, fxs.merged * f, fxs.budget * f, fxs.rest);
    for (int pick = 0; pick < 6 && n > 0 && (size_t)n < cap; pick++) {
        int best = -1;
        for (int i = 0; i < 24; i++)
            if (fxs.t[i].n && (best < 0 || fxs.t[i].px > fxs.t[best].px))
                best = i;
        if (best < 0)
            break;
        n += snprintf(out + n, cap - n, " %08lx%s:%.2f/%.0f", (unsigned long)fxs.t[best].tex, fxs.t[best].add ? "+" : "",
                      fxs.t[best].px * f, fxs.t[best].n * f);
        fxs.t[best].n = 0;
    }
    memset(&fxs, 0, sizeof fxs);
}
#endif

static void draw_fx(Renderer *r, const FxQuad *fx, int nfx, const Camera *cam, float fog_start, float fog_end, int world)
{
    XS_END();
#if defined RAMPAGE_XBOX && defined RAMPAGE_XBOX_TEST_ARGS
    float fxs_pm[16], fxs_frame = 0;
    {
        float P[16], V[16];
        glGetFloatv(GL_PROJECTION_MATRIX, P);
        glGetFloatv(GL_MODELVIEW_MATRIX, V);
        for (int c0 = 0; c0 < 4; c0++)
            for (int r0 = 0; r0 < 4; r0++)
                fxs_pm[c0 * 4 + r0] = P[r0] * V[c0 * 4] + P[4 + r0] * V[c0 * 4 + 1] + P[8 + r0] * V[c0 * 4 + 2] +
                                      P[12 + r0] * V[c0 * 4 + 3];
    }
#endif
    float M[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, M);
    float right[3] = { M[0], M[4], M[8] }, up[3] = { M[1], M[5], M[9] };
    GLboolean fogged = glIsEnabled(GL_FOG);
    glDisable(GL_FOG);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glAlphaFunc(GL_GREATER, 0.004f);
    float fog_k = fog_end > fog_start ? 1 / (fog_end - fog_start) : 0;
#ifdef RAMPAGE_XBOX
    GLuint qb_id = ~0u;
    int qb_depth = -1, qb_add = -1, qb_flat = -1;
    const Bundle *last_b = NULL;                           /* (an emitter's quads follow each other: its texture is
                                                            * looked up once, not for each of a thousand quads) */
    uint32_t last_tex = 0;
    GLuint last_id = 0;
    /* The world's effects within what the GPU has time to blend (g_xbox_fx_budget). The game's smoke and dust are
     * hundreds of soft quads over each other, sized for a PS2 that fills pixels several times faster than this
     * console and had a third of the pixels of 1280 x 720; a building coming down covers the screen nine times over
     * and the frame takes twice as long. When this frame's quads cover more than the budget, a share of the
     * particles is left out, the same ones from frame to frame (FxQuad.key; those next to go are faded, so none
     * pops), and the ones drawn are made as much denser as it takes to hide what all of them hid (fx_denser; an
     * additive quad as much brighter). A particle the game draws several times over (int.multiple_render) is
     * drawn once, denser the same way. Never under a seventh of them; back up slowly, down at once. */
    int govern = world && g_xbox_fx_budget > 0;
    float keep = 1;
    if (govern) {
        float P[16], total = 0;
        glGetFloatv(GL_PROJECTION_MATRIX, P);
        for (int i = 0; i < nfx; i++)
            if (!i || !fx_same(&fx[i], &fx[i - 1]))
                total += fx_share(&fx[i], M, P[0], P[5]);
        float budget = (float)g_xbox_fx_budget * 0.01f * (1280.0f * 720.0f) /
                       ((float)xbox_pixel_width() * (float)xbox_pixel_height());
        static float keep_was = 1, rest_ms = 27.0f, drawn;
        static double frame_at;
        if (g_xbox_fx_frame_ms > 0 && xbox_pixel_height() > 480) {
            double at = win_time();
            float took = (float)((at - frame_at) * 1000.0);
            frame_at = at;
            if (took > 10.0f && took < 100.0f)             /* (not a load, nor the first frame) */
                rest_ms += (took - FX_SCREEN_MS * drawn - rest_ms) * 0.05f;
            float room = ((float)g_xbox_fx_frame_ms - rest_ms) / FX_SCREEN_MS;
            budget = room < 0.25f * budget ? 0.25f * budget : room > 1.5f * budget ? 1.5f * budget : room;
        }
        float want = total > budget ? budget / total : 1;
        if (want < 0.15f)
            want = 0.15f;
        keep = want < keep_was ? want : fminf(want, keep_was + 0.02f);
        keep_was = keep;
        drawn = total * keep;
#ifdef RAMPAGE_XBOX_TEST_ARGS
        fxs.asked += total;
        fxs.keep += keep;
        fxs.budget += budget;
        fxs.rest = rest_ms;
#endif
    }
    qb_begin();
#endif
    for (int i = 0; i < nfx; i++) {
        const FxQuad *q = &fx[i];
#ifdef RAMPAGE_XBOX
        float fx_weight = 1, fx_times = 1;                 /* (above) this quad's fade, and how many quads it stands for */
        if (govern) {
            int run = 1;
            while (i + run < nfx && fx_same(&fx[i + run], q))
                run++;
            i += run - 1;
            fx_times = (float)run;
#ifdef RAMPAGE_XBOX_TEST_ARGS
            fxs.merged += run - 1;
#endif
            if (keep < 1) {
                float h = (float)q->key * (1.0f / 65536.0f);
                if (h >= keep)
                    continue;
                if (h > 0.8f * keep)
                    fx_weight = (keep - h) / (0.2f * keep);
                fx_times /= 0.9f * keep;                   /* (the faded fifth counts for half) */
            }
        }
#endif
#ifdef RAMPAGE_XBOX
        GLuint id = 0;
        if (q->b) {
            if (q->b != last_b || q->tex != last_tex) {
                last_id = texture_at(r, q->b, q->tex, 1);
                last_b = q->b;
                last_tex = q->tex;
            }
            id = last_id;
        }
#else
        GLuint id = q->b ? texture_at(r, q->b, q->tex, 1) : 0;
#endif
#ifdef RAMPAGE_XBOX
        {                                                  /* the state as below, set only when it changes */
            int depth = q->ztest || world, add = q->additive != 0, flat = q->kind == FXQ_FLAT;
            if (id != qb_id || depth != qb_depth || add != qb_add || flat != qb_flat) {
                qb_flush();
                if (id != qb_id) {
                    if (id) {
                        glEnable(GL_TEXTURE_2D);
                        glBindTexture(GL_TEXTURE_2D, id);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    } else
                        glDisable(GL_TEXTURE_2D);
                }
                if (depth != qb_depth) { if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST); }
                if (add != qb_add) glBlendFunc(GL_SRC_ALPHA, add ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
                if (flat != qb_flat) {
                    if (flat) { glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1, -4); }
                    else glDisable(GL_POLYGON_OFFSET_FILL);
                }
                qb_id = id; qb_depth = depth; qb_add = add; qb_flat = flat;
            }
        }
#else
        if (id) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, id);
            XS_TEXTURE_BOUND();
            /* [R] a particle's shader (the emitter's material, 0x1F8870: 0x57 or 0x55) clamps: its descriptor's
             * first pass repeat byte (+0x8D) is 0, its record's wrap 0x10 (0x265840 -> 0x26AA20: CLAMP_1 CLAMP) */
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        } else {
            glDisable(GL_TEXTURE_2D);
        }
        if (q->ztest || world) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        glBlendFunc(GL_SRC_ALPHA, q->additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
#endif
        float c[4][3];                                     /* top-left, top-right, bottom-right, bottom-left */
        if (q->kind == FXQ_BILLBOARD) {
#ifdef RAMPAGE_XBOX
            float cs = 1, sn = 0, rr[3], uu[3];          /* (the library's cosf and sinf are slow here, and most
                                                          * billboards are not turned: fx.c's table) */
            if (q->rot != 0)
                fx_sincos(q->rot, &sn, &cs);
#else
            float cs = cosf(q->rot), sn = sinf(q->rot), rr[3], uu[3];
#endif
            for (int k = 0; k < 3; k++) {
                rr[k] = (cs * right[k] + sn * up[k]) * q->size[0] / 2;
                uu[k] = (-sn * right[k] + cs * up[k]) * q->size[1] / 2;
            }
            for (int k = 0; k < 3; k++) {
                c[0][k] = q->pos[k] - rr[k] + uu[k];
                c[1][k] = q->pos[k] + rr[k] + uu[k];
                c[2][k] = q->pos[k] + rr[k] - uu[k];
                c[3][k] = q->pos[k] - rr[k] - uu[k];
            }
        } else if (q->kind == FXQ_FLAT) {
            for (int k = 0; k < 3; k++) {
                c[0][k] = q->pos[k] - q->axis[0][k] + q->axis[1][k];
                c[1][k] = q->pos[k] + q->axis[0][k] + q->axis[1][k];
                c[2][k] = q->pos[k] + q->axis[0][k] - q->axis[1][k];
                c[3][k] = q->pos[k] - q->axis[0][k] - q->axis[1][k];
            }
        } else {
            /* a streak [R 0x1F7500]: tail to head in the camera's plane, d their direction there ((-1, 0) when shorter
             * than sqrt(0.001)), n = d turned a quarter counter-clockwise, half the width w; the corners tail + w(n - d),
             * tail - w(n + d), head + w(n + d), head + w(d - n), so the quad runs w past each end; their uv (0x3B2550):
             * u 1 at the tail and 0 at the head, v 0 on the n side: the texture's width lies along the streak (the
             * bomb's nose, at its picture's right, leads it down) */
            const float *a = q->axis[0];
            float dx = 2 * (a[0] * right[0] + a[1] * right[1] + a[2] * right[2]);
            float dy = 2 * (a[0] * up[0] + a[1] * up[1] + a[2] * up[2]), l2 = dx * dx + dy * dy, w = q->size[1] / 2;
            if (l2 <= 0.001f) {
                dx = -1;
                dy = 0;
            } else {
                float l = sqrtf(l2);
                dx /= l;
                dy /= l;
            }
            for (int k = 0; k < 3; k++) {
                float d = w * (dx * right[k] + dy * up[k]), n = w * (-dy * right[k] + dx * up[k]);
                float head = q->pos[k] + a[k], tail = q->pos[k] - a[k];
                c[0][k] = head + n + d;                    /* u0 v0 */
                c[1][k] = tail + n - d;                    /* u1 v0 */
                c[2][k] = tail - n - d;                    /* u1 v1 */
                c[3][k] = head - n + d;                    /* u0 v1 */
            }
        }
        /* lying on the street: pulled in front of it. The street writes depth [R: every block ground part is a kind 2
         * material, 162 of 167 flags 0xC: shader 1 (0x16F030's table 0x3B1680), one pass, GS state 1, TEST 0x50002 (Z
         * test GEQUAL, Z written); the rest shader 2 / 0xC, Z written too], so a quad in its plane is cut by it on the
         * PS2 as here, where its depth rounds behind; the offset stands for GEQUAL's equal depth passing [G: how two
         * coplanar draws round on the GS is the hardware's] */
#ifndef RAMPAGE_XBOX
        if (q->kind == FXQ_FLAT) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1, -4);
        } else
            glDisable(GL_POLYGON_OFFSET_FILL);
#endif
        float a[4];                                        /* the alpha x the fog factor at each corner */
        for (int k = 0; k < 4; k++) {
            float f = 1;
            if (fog_k > 0) {
                float d = -(M[2] * c[k][0] + M[6] * c[k][1] + M[10] * c[k][2] + M[14]);
                f = (fog_end - d) * fog_k;
                f = f < 0 ? 0 : f > 1 ? 1 : f;
            }
            a[k] = f;
        }
        float qa = q->col[3];
        const float *qcol = q->col;
#ifdef RAMPAGE_XBOX
        float denser[3];
        if (fx_times > 1) {
            if (q->additive) {                             /* (added: n quads are one n times as bright) */
                qa *= fx_times;
                if (qa > 1) {
                    for (int k = 0; k < 3; k++)
                        denser[k] = fminf(q->col[k] * qa, 1.0f);
                    qcol = denser;
                    qa = 1;
                }
            } else
                qa = fx_denser(qa, fx_times);
        }
        qa *= fx_weight;
#endif
        for (int k = 0; k < 4; k++)
            a[k] *= qa;
        if (world && !q->ztest) {                          /* (above: tested in front of what it starts in) */
            float pull = 0;
            for (int k = 0; k < 4; k++) {                  /* (the farthest corner: one root, of the largest square) */
                float e[3] = { c[k][0] - q->pos[0], c[k][1] - q->pos[1], c[k][2] - q->pos[2] };
                pull = fmaxf(pull, e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
            }
            pull = sqrtf(pull) + 1;
            for (int k = 0; k < 4; k++) {
                float e[3] = { c[k][0] - cam->pos[0], c[k][1] - cam->pos[1], c[k][2] - cam->pos[2] };
                float d = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]), f = d > 0 ? fmaxf(d - pull, 0.5f) / d : 1;
                for (int j = 0; j < 3; j++)
                    c[k][j] = cam->pos[j] + e[j] * f;
            }
        }
        static const int UV[4][2] = { { 0, 1 }, { 2, 1 }, { 2, 3 }, { 0, 3 } };
#if defined RAMPAGE_XBOX && defined RAMPAGE_XBOX_TEST_ARGS
        if (world) {                                       /* (above: the share of the screen this quad covers) */
            float sx[4], sy[4], share = 0;
            int front = 1;
            for (int k = 0; k < 4 && front; k++) {
                const float *m = fxs_pm;
                float cw = m[3] * c[k][0] + m[7] * c[k][1] + m[11] * c[k][2] + m[15];
                if (cw <= 0.05f) { front = 0; break; }
                sx[k] = (m[0] * c[k][0] + m[4] * c[k][1] + m[8] * c[k][2] + m[12]) / cw;
                sy[k] = (m[1] * c[k][0] + m[5] * c[k][1] + m[9] * c[k][2] + m[13]) / cw;
            }
            if (!front) {
                share = 0.5f;                              /* (through the camera's plane: counted as half a screen) */
                fxs.near++;
            } else {
                float area = 0, x0 = sx[0], x1 = sx[0], y0 = sy[0], y1 = sy[0];
                for (int k = 0; k < 4; k++) {
                    int j = (k + 1) & 3;
                    area += sx[k] * sy[j] - sx[j] * sy[k];
                    if (sx[k] < x0) x0 = sx[k];
                    if (sx[k] > x1) x1 = sx[k];
                    if (sy[k] < y0) y0 = sy[k];
                    if (sy[k] > y1) y1 = sy[k];
                }
                area = fabsf(area) * 0.5f;
                float box = (x1 - x0) * (y1 - y0);
                float cx0 = x0 < -1 ? -1 : x0, cx1 = x1 > 1 ? 1 : x1, cy0 = y0 < -1 ? -1 : y0, cy1 = y1 > 1 ? 1 : y1;
                if (box > 0 && cx1 > cx0 && cy1 > cy0)
                    share = area * ((cx1 - cx0) * (cy1 - cy0) / box) * 0.25f;
            }
            fxs.quads++;
            if (a[0] <= 8 / 255.0f && a[1] <= 8 / 255.0f && a[2] <= 8 / 255.0f && a[3] <= 8 / 255.0f)
                fxs.faint++;
            fxs.px += share;
            fxs_frame += share;
            if (share > 1 / 16.0f)
                fxs.big += share;
            int slot = 0;
            while (slot < 23 && fxs.t[slot].n && (fxs.t[slot].tex != q->tex || fxs.t[slot].add != (q->additive != 0)))
                slot++;
            fxs.t[slot].tex = q->tex;
            fxs.t[slot].add = q->additive != 0;
            fxs.t[slot].px += share;
            fxs.t[slot].n++;
        }
#endif
#ifdef RAMPAGE_XBOX
        for (int k = 0; k < 4; k++)
            qb_vertex(k, c[k][0], c[k][1], c[k][2], q->uv[UV[k][0]], q->uv[UV[k][1]], qcol, a[k]);
        qb_next();
#else
        glBegin(GL_QUADS);
        for (int k = 0; k < 4; k++) {
            glColor4f(qcol[0], qcol[1], qcol[2], a[k]);
            glTexCoord2f(q->uv[UV[k][0]], q->uv[UV[k][1]]);
            glVertex3fv(c[k]);
        }
        glEnd();
#endif
    }
#ifdef RAMPAGE_XBOX
    qb_end();
    XS_TEXTURE_BOUND();
#endif
#if defined RAMPAGE_XBOX && defined RAMPAGE_XBOX_TEST_ARGS
    if (world) {
        fxs.frames++;
        if (fxs_frame > fxs.peak)
            fxs.peak = fxs_frame;
    }
#endif
    glDisable(GL_POLYGON_OFFSET_FILL);
    if (fogged)
        glEnable(GL_FOG);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
}

void rnd_frame(Renderer *r, const Window *w, const Camera *cam, double t, int fog)
{
    XS_END();
#ifdef RAMPAGE_XBOX
    r->xframe++;
    gliSkinNewFrame();                               /* (the skinning programs: loaded again at their first use) */
    qb_frame();
    r->own_budget = 64u * 1024u;                     /* (about 1,300 vertices: half a millisecond of copying) */
    r->own_starved = OWN_STARVED_BYTES;
    r->own_made = 0;
#endif
    pump_output(r);
    const Scene *s = r->scene;
    r->drawn = 0;
    if (r->tex_gen != g_tex_gen) {                         /* the texture settings changed: loaded anew */
        drop_textures(r);
        r->tex_gen = g_tex_gen;
    }
#ifdef RAMPAGE_XBOX
    glViewport(0, 0, xbox_pixel_width(), xbox_pixel_height());   /* (a 16:9 screen: the window is 853 wide, the pixels 640) */
#else
    glViewport(0, 0, w->width, w->height);
#endif
    float sky[3] = { 0.47f, 0.6f, 0.89f };
    if (s && s->fog_end > 0)
        memcpy(sky, s->fog_colour, sizeof sky);
    if (getenv("RAMPAGE_MAGENTA")) { sky[0] = 1; sky[1] = 0; sky[2] = 1; }
    if (s && s->nitems)                                    /* port extra: MSAA / SMAA draw the 3D picture into their
                                                            * target (postfx.c; nothing while they are off) */
        postfx_begin(w->width, w->height);
#ifdef RAMPAGE_XBOX
    /* (g_xbox_clear_color: with a sky the colour waits until it is known whether the sky is drawn late) */
    int far_fill = g_xbox_clear_color != 1 && s && s->sky >= 0;
    if (g_xbox_clear_color == 2)
        glClearColor(1, 0, 1, 1);
    else
        glClearColor(sky[0], sky[1], sky[2], 1);
    glClear(far_fill && g_xbox_clear_color != 2 ? GL_DEPTH_BUFFER_BIT : GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
#else
    glClearColor(sky[0], sky[1], sky[2], 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
#endif
    if (!s)
        return;
    float aspect = w->height ? (float)w->width / w->height : 1;
    perspective(cam->fov_deg, aspect, 0.5f, 3000);
    look(cam);
    glEnable(GL_DEPTH_TEST);
    glCullFace(GL_BACK);                                   /* one side, but for the two-sided materials (draw_mesh) */
    glFrontFace(GL_CCW);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    glEnable(GL_NORMALIZE);
    glEnable(GL_LIGHTING);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE);      /* a two-sided one's back lit as its front (no flip) */
    float amb[4] = { s->ambient[0], s->ambient[1], s->ambient[2], 1 };
    if (!s->nlights)
        amb[0] = amb[1] = amb[2] = 1;
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
    for (int i = 0; i < 4; i++) {
        if (i >= s->nlights) {
            glDisable(GL_LIGHT0 + i);
            continue;
        }
        const float *d = s->light_dir[i];
        float to_light[4] = { -d[0], -d[1], -d[2], 0 }, col[4] = { s->light_colour[i][0], s->light_colour[i][1], s->light_colour[i][2], 1 };
        float zero[4] = { 0, 0, 0, 1 };
        glLightfv(GL_LIGHT0 + i, GL_POSITION, to_light);  /* with the view matrix loaded: world-space directions */
        glLightfv(GL_LIGHT0 + i, GL_DIFFUSE, col);
        glLightfv(GL_LIGHT0 + i, GL_AMBIENT, zero);
        glLightfv(GL_LIGHT0 + i, GL_SPECULAR, zero);
        glEnable(GL_LIGHT0 + i);
    }
    /* FogStart / FogEnd are not distances but world y: the game's camera manager (RCameraMgr 0x2BFDA0, every frame;
     * +0x210 set by its init 0x2BFF60) makes each the eye depth of the street point (camera x, FogStart, 0):
     * forward.y x (y - camera y) - forward.z x camera z; those go to the VU1 program's F = 255 (far - w) / (far -
     * near), w the eye-plane depth (0x25A510, 0x37A2A0), which is GL_LINEAR's. So the street, the monsters and the
     * fronts are not fogged; the blocks behind and the backdrop are [R] */
    float fwd[3] = { cosf(cam->pitch) * cosf(cam->yaw), cosf(cam->pitch) * sinf(cam->yaw), sinf(cam->pitch) };
    if (cam->use_rows)
        memcpy(fwd, cam->rows[1], sizeof fwd);
    float fog_near = fwd[1] * (s->fog_start - cam->pos[1]) - fwd[2] * cam->pos[2];
    float fog_far = fwd[1] * (s->fog_end - cam->pos[1]) - fwd[2] * cam->pos[2];
    int fogged = fog && s->fog_end > 0 && fog_far > fog_near;   /* (a free camera facing -y: none) */
    if (fogged) {
        float fc[4] = { s->fog_colour[0], s->fog_colour[1], s->fog_colour[2], 1 };
        glEnable(GL_FOG);
        glFogi(GL_FOG_MODE, GL_LINEAR);
        glFogfv(GL_FOG_COLOR, fc);
        glFogf(GL_FOG_START, fog_near);
        glFogf(GL_FOG_END, fog_far);
    } else {
        glDisable(GL_FOG);
    }
    glEnableClientState(GL_VERTEX_ARRAY);
    /* the sky first, around the camera (0x2A53E0): x 0.01 at the camera's (x, y, 0.99 z), the depth test off
     * (GS TEST 0x31001) and no fog (PRIM.FGE cleared for its state 0xB), so it is a background that nothing shows
     * through and that hides nothing */
#ifdef RAMPAGE_XBOX
    /* The sky after the street and the batched buildings (below), which needs it behind them by its depth. The game
     * draws it a hundredth of its size about a point just under the camera with the depth test off; the same
     * picture comes from any size k drawn about (camera x, camera y, camera z (1 - k)): every point of it stays in
     * the same direction from the camera. So it is drawn large, its farthest point 2,700 units away (the far plane
     * is at 3,000), with the ordinary depth test. Not when its nearest point would then be closer than 1,200 units
     * (a sky that is not a dome about the camera): first and untested, as before. */
    float sky_k = 0;
    if (g_xbox_sky_late && s->sky >= 0) {
        float dmin = 1e30f, dmax = 0;
        for (uint32_t i = 0; i < s->nitems; i++) {
            const DrawItem *it = &s->items[i];
            if (it->instance != s->sky || it->skin || it->mesh >= s->nmeshes)
                continue;
            const Mesh *m = &s->meshes[it->mesh].mesh;
            const float *W = it->world.m;
            for (uint32_t q = 0; q < m->nparts; q++)
                for (uint32_t v = 0; v < m->parts[q].nverts; v++) {
                    const float *a = m->parts[q].pos + 3 * v;
                    float x = W[0] * a[0] + W[4] * a[1] + W[8] * a[2] + W[12];
                    float y = W[1] * a[0] + W[5] * a[1] + W[9] * a[2] + W[13];
                    float z = W[2] * a[0] + W[6] * a[1] + W[10] * a[2] + W[14] - cam->pos[2];
                    float d = x * x + y * y + z * z;
                    if (d < dmin) dmin = d;
                    if (d > dmax) dmax = d;
                }
        }
        if (dmax > 0) {
            float k = 2700.0f / sqrtf(dmax);
            if (k * sqrtf(dmin) >= 1200.0f)
                sky_k = k;
        }
    }
    int sky_late = sky_k > 0;
#else
    int sky_late = 0;
#endif
#ifdef RAMPAGE_XBOX
    if (XSKIP(1))
        sky_late = 0;
    if (far_fill && !sky_late) {                     /* (the sky first, as the PS2: the frame cleared after all) */
        if (g_xbox_clear_color != 2)
            glClear(GL_COLOR_BUFFER_BIT);
        far_fill = 0;
    }
    if (s->sky >= 0 && !sky_late && !XSKIP(1)) {
#else
    if (s->sky >= 0 && !sky_late) {
#endif
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_FOG);
        for (uint32_t i = 0; i < s->nitems; i++) {
            const DrawItem *it = &s->items[i];
            if (it->instance != s->sky)
                continue;
            glPushMatrix();
            glTranslatef(cam->pos[0], cam->pos[1], 0.99f * cam->pos[2]);
            glScalef(0.01f, 0.01f, 0.01f);
            glMultMatrixf(it->world.m);
            draw_mesh(r, &s->meshes[it->mesh], it->skin, t);
            glPopMatrix();
        }
        XS_END();
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        if (fogged)
            glEnable(GL_FOG);
    }
    float view_mv[16], ty = tanf(cam->fov_deg * 3.14159265f / 360.0f), tx = ty * aspect;
    glGetFloatv(GL_MODELVIEW_MATRIX, view_mv);
#ifdef RAMPAGE_XBOX
    int xbox_mv_loaded = 0;
    uint32_t rig_skip = 0;
#endif
    for (int pass = 0; pass < 2; pass++) {               /* the street, its building shadows, then the rest */
        r->hold = pass && gtao_active();                 /* port extra: the blended parts after the occlusion (the
                                                          * street's lie on it, darkened as it is) */
#ifdef RAMPAGE_XBOX
        if (g_xbox_item_seen_n != s->nitems) {           /* (a new set of items: all taken as seen) */
            uint8_t *seen = realloc(g_xbox_item_seen, s->nitems ? s->nitems : 1);
            if (seen) {
                memset(seen, 1, s->nitems ? s->nitems : 1);
                g_xbox_item_seen = seen;
                g_xbox_item_seen_n = s->nitems;
            } else
                g_xbox_item_seen_n = 0;
        }
        if (pass) {                                      /* the pass's culling first: the batches send what is in sight */
            for (uint32_t i = 0; i < g_xbox_item_seen_n; i++) {
                const DrawItem *it = &s->items[i];
                const Instance *in = &s->instances[it->instance];
                if (!in->hidden && it->instance != s->sky && it->instance != s->ground_inst)
                    g_xbox_item_seen[i] = (uint8_t)item_seen(r, it, view_mv, tx, ty);
            }
            memcpy(r->bat_amb, amb, sizeof r->bat_amb);
            r->bat_own = 0;
            bat_frame(r, t);
            if (sky_late) {
                /* The sky now, not first. Drawn first it is a textured fill of the whole screen that the street and
                 * the buildings then cover almost entirely: at 1280 x 720 close to a million pixels a frame the
                 * console's GPU has no time for. Here it is drawn after the street and the batched buildings, at
                 * its distance (sky_k, above) and tested against what is there, so it costs only where it shows;
                 * its depth is still not written, and what comes after (monsters, people, the blended parts, the
                 * effects) is drawn over it as before. */
                if (far_fill)
                    xbox_far_fill(sky, tx, ty);
                glLoadMatrixf(view_mv);
                glDepthMask(GL_FALSE);
                glDisable(GL_FOG);
                for (uint32_t i = 0; i < s->nitems; i++) {
                    const DrawItem *it = &s->items[i];
                    if (it->instance != s->sky)
                        continue;
                    glPushMatrix();
                    glTranslatef(cam->pos[0], cam->pos[1], (1.0f - sky_k) * cam->pos[2]);
                    glScalef(sky_k, sky_k, sky_k);
                    glMultMatrixf(it->world.m);
                    draw_mesh(r, &s->meshes[it->mesh], it->skin, t);
                    glPopMatrix();
                }
                XS_END();
                glDepthMask(GL_TRUE);
                if (fogged)
                    glEnable(GL_FOG);
                glLoadMatrixf(view_mv);
                xbox_mv_loaded = 0;
            }
        }
#endif
        for (uint32_t i = 0; i < s->nitems; i++) {
            const DrawItem *it = &s->items[i];
            const Instance *in = &s->instances[it->instance];
            if (in->hidden || it->instance == s->sky || (it->instance == s->ground_inst) != !pass)
                continue;
#ifdef RAMPAGE_XBOX
            if (XSKIP(pass ? 16 : 2) || (pass && XSKIP(it->skin ? 512 : 1024)))
                continue;
#endif
#ifdef RAMPAGE_XBOX
            int seen = pass && i < g_xbox_item_seen_n ? g_xbox_item_seen[i] : item_seen(r, it, view_mv, tx, ty);
            if (i < g_xbox_item_seen_n)
                g_xbox_item_seen[i] = (uint8_t)seen;     /* (scene_update: a skin out of sight is not posed) */
            r->bat_keys = pass && i < r->nbitem && r->bitem[i].active ? r->bitem[i].key : NULL;
            r->cur_inst = in;
            /* A model that keeps moving (a person, a soldier, a car: not in the batches, and moved more than once),
             * the run of its unskinned items: their merged meshes in a few draws (rig_mesh), then whatever parts
             * of them were not merged, item by item. */
            if (pass && g_xbox_rigid && i >= rig_skip && !it->skin && i < r->nbitem && !r->bitem[i].active &&
                r->bitem[i].moves >= 3) {
                uint32_t n = 1;
                while (i + n < s->nitems && n < SKIN_GROUP_BONES && s->items[i + n].instance == it->instance &&
                       !s->items[i + n].skin && i + n < r->nbitem && !r->bitem[i + n].active)
                    n++;
                if (n >= 2) {
                    int vis = 0;
                    for (uint32_t j = 0; j < n; j++)
                        vis |= i + j < g_xbox_item_seen_n ? g_xbox_item_seen[i + j] : 1;
                    RigMesh *rm = vis ? rig_mesh(r, i, n, t) : NULL;
                    if (!vis) {
                        i += n - 1;
                        continue;
                    }
                    if (rm) {
                        int own_amb = in->own_ambient && s->nlights;
                        if (own_amb)
                            glLightModelfv(GL_LIGHT_MODEL_AMBIENT,
                                           (float[4]){ in->ambient[0], in->ambient[1], in->ambient[2], 1 });
                        glLoadMatrixf(view_mv);      /* (the palette holds the pieces' world matrices) */
                        xbox_mv_loaded = 0;
                        int merged = rig_draw(r, rm, i);
                        for (uint32_t j = 0; j < n; j++) {
                            if (merged && !rm->left[j])
                                continue;
                            const DrawItem *jt = &s->items[i + j];
                            r->bat_keys = merged ? rm->keys[j] : NULL;
                            xbox_item_matrix(view_mv, jt->world.m);
                            xbox_mv_loaded = 1;
                            draw_mesh(r, &s->meshes[jt->mesh], NULL, t);
                        }
                        r->bat_keys = NULL;
                        if (own_amb)
                            glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
                        i += n - 1;
                        continue;
                    }
                    rig_skip = i + n;                /* not merged: its items one by one, and no shorter run of
                                                      * them tried as a model of its own */
                }
            }
#else
            int seen = item_seen(r, it, view_mv, tx, ty);
#endif
            if (!seen)
                continue;
#ifdef RAMPAGE_XBOX
            /* An item whose parts are all in batches has nothing left to draw here: no ambient, no matrix (each
             * crossed gl4es, for several hundred standing items a frame). */
            if (g_xbox_item_skip && r->bat_keys && !it->skin &&
                r->bitem[i].nkeys >= s->meshes[it->mesh].mesh.nparts) {
                const Mesh *im = &s->meshes[it->mesh].mesh;
                uint32_t k = 0;
                while (k < im->nparts && (!im->parts[k].ntris || r->bat_keys[k] >= 0))
                    k++;
                if (k == im->nparts)
                    continue;
            }
#endif
            int own = in->own_ambient && s->nlights;   /* an adornment's ambient (0x2CD060), the scene's after it */
            if (own)
                glLightModelfv(GL_LIGHT_MODEL_AMBIENT, (float[4]){ in->ambient[0], in->ambient[1], in->ambient[2], 1 });
#ifdef RAMPAGE_XBOX
            xbox_item_matrix(view_mv, it->world.m);
            xbox_mv_loaded = 1;
            draw_mesh(r, &s->meshes[it->mesh], it->skin, t);
#else
            glPushMatrix();
            glMultMatrixf(it->world.m);
            draw_mesh(r, &s->meshes[it->mesh], it->skin, t);
            glPopMatrix();
#endif
            if (own)
                glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
        }
#ifdef RAMPAGE_XBOX
        if (xbox_mv_loaded) {                        /* the view matrix again, for what follows */
            glLoadMatrixf(view_mv);
            xbox_mv_loaded = 0;
        }
#endif
#ifdef RAMPAGE_XBOX
        r->bat_keys = NULL;
        r->cur_inst = NULL;
        if (pass)
            bat_draw_blended(r);
#endif
        if (!pass && s->shadows)
            draw_shadows(r, SHADOW_PASS_GROUND);
    }
    for (int i = 0; i < s->nfxm; i++) {                    /* the effects' mesh debris, drawn as props (0x21DC20) */
        const FxMesh *m = &s->fxm[i];
        glPushMatrix();
        glMultMatrixf(m->m);
        for (int k = 0; k < s->fx_nshapes[m->shape]; k++)
            draw_mesh(r, &s->fx_shapes[m->shape][k], NULL, t);
        glPopMatrix();
    }
    if (r->hold) {                                         /* port extra: the ambient occlusion over the opaque world
                                                            * (gtao.c), faded out with the fog, then what it held back */
        float f = 1.0f / tanf(cam->fov_deg * 3.14159265f / 360.0f);
        gtao_frame(0.5f, 3000, f / aspect, f, fogged ? fog_near : 0, fogged ? fog_far : 0);
        r->hold = 0;
        draw_held(r, t);
    }
    if (s->nwater)
        draw_water(r, s, cam);
    if (s->shadows) {
        draw_shadows(r, SHADOW_PASS_DECAL);
        draw_shadows(r, SHADOW_PASS_WALL);
    }
    XS_END();
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisable(GL_CULL_FACE);
#ifdef RAMPAGE_XBOX
    if (s->nfx && !XSKIP(32))
#else
    if (s->nfx)
#endif                                            /* the effects' particles, over the world */
        draw_fx(r, s->fx, s->nfx, cam, fogged ? fog_near : 0, fogged ? fog_far : 0, 1);
    if (s->nspheres) {                                     /* the overlay: three great circles per sphere */
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_FOG);
        glColor3f(1, 0.2f, 0.1f);
        for (int i = 0; i < s->nspheres; i++) {
            const float *c = s->spheres[i];
            for (int axis = 0; axis < 3; axis++) {
                glBegin(GL_LINE_LOOP);
                for (int k = 0; k < 32; k++) {
                    float a = k * 6.2831853f / 32, u = c[3] * cosf(a), v = c[3] * sinf(a);
                    float p[3] = { c[0], c[1], c[2] };
                    p[(axis + 1) % 3] += u;
                    p[(axis + 2) % 3] += v;
                    glVertex3fv(p);
                }
                glEnd();
            }
        }
        glEnable(GL_DEPTH_TEST);
        glColor3f(1, 1, 1);
    }
    postfx_end();                                          /* the HUD (and the shell's 3D layers in it) stay sharp */
#ifdef RAMPAGE_XBOX
    if (s->nhud && !XSKIP(64))
#else
    if (s->nhud)
#endif
        draw_hud(r, s, w->width, w->height, t);
}
