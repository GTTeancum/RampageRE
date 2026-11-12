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
    uint8_t churn;                       /* times its registered arrays changed: past 3 it is left to the staging */
    uint8_t wait;                        /* draws on its own before its arrays are registered (vbo 3: waiting) */
    uint8_t skinned;                     /* drawn posed: never registered */
    const float *gpos, *gnrm, *guv, *gcol;       /* its registered arrays in GPU memory (gliStaticArrayAddress) */
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
void gliSkinDraw(const float *pos, const float *nrm, const float *uv, const float *col, const float *bone,
                 const float *weight, const uint16_t *idx, int count, const float (*palette)[12], int nbones);
int gl4es_xbox_predraw(void);

/* A skinned part for the GPU: its triangles in groups that each use at most the vertex program's bones, a group's
 * vertices (the bind pose, with each one's bones as indices into the group's list, and their weights) in the static
 * store. Made once for a mesh part and shared by every instance drawn with it. */
#define SKIN_GROUP_BONES 26
#define SKIN_GROUP_VERTS 16000u                  /* (16 bytes a vertex in the largest array, a 256 KB chunk) */
typedef struct {
    float *pos, *nrm, *uv, *col, *bone, *weight;
    uint16_t *idx;
    uint32_t nverts, nidx;
    uint8_t nb, b[SKIN_GROUP_BONES];             /* its bones: indices into the instance's palette */
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
    pb->gpos = pb->gnrm = pb->guv = pb->gcol = NULL;
    if (pb->vbo == 1) {
        gliStaticArrayRemove(pb->pos, pb);
        if (pb->nrm) gliStaticArrayRemove(pb->nrm, pb);
        if (pb->uv) gliStaticArrayRemove(pb->uv, pb);
        if (pb->col) gliStaticArrayRemove(pb->col, pb);
    }
    pb->vbo = 0;
    free(pb->idx16);
    pb->idx16 = NULL;
}
#endif

#ifdef RAMPAGE_XBOX
static void bat_drop(Renderer *r);
#endif

static void drop_buffers(Renderer *r)
{
#ifdef RAMPAGE_XBOX
    bat_drop(r);
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
static const PartBuf *xbox_part(Renderer *r, const SceneMesh *sm, uint32_t i, int skinned)
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
    if (!pb->vbo || pb->rev != p->rev || pb->nverts != p->nverts || pb->ntris != p->ntris || pb->pos != p->pos ||
        pb->nrm != p->nrm || pb->uv != p->uv || pb->col != p->col || pb->tris != p->tris ||
        pb->skinned != (uint8_t)(skinned != 0)) {
        if (pb->vbo == 1 && pb->churn < 255)
            pb->churn++;                             /* (a facade being damaged: registering it again and again
                                                      * costs more than staging it) */
        xbox_static_unregister(pb);
        size_t n = p->nverts;
        pb->rev = p->rev; pb->nverts = p->nverts; pb->ntris = p->ntris;
        pb->pos = p->pos; pb->nrm = p->nrm; pb->uv = p->uv; pb->col = p->col; pb->tris = p->tris;
        if (n <= 65536 && p->ntris && p->tris && (pb->idx16 = malloc(sizeof *pb->idx16 * 3 * (size_t)p->ntris)))
            for (size_t k = 0; k < 3 * (size_t)p->ntris; k++)
                pb->idx16[k] = (uint16_t)p->tris[k];
        pb->vbo = pb->churn <= 3 && n && p->pos && (!skinned || p->uv || p->col) ? 3 : 2;
        pb->wait = 0;
        pb->skinned = (uint8_t)(skinned != 0);
    }
    /* Registered only once it has been drawn on its own for a second and a half: a part on its way into a batch (its
     * item settling) would leave a second copy of itself in the store, and the store is small. */
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
        int decoded = tex_decode(b, bdp_entry_of(b, name, ET_TEXTURE), &rgba, &w, &h);
        if (decoded == TEX_DECODE_NOMEM) {
            slots[name] = 0;             /* missing/invalid images stay negative-cached; OOM can retry */
            return 0;
        }
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
uint8_t *g_xbox_item_seen;
uint32_t g_xbox_item_seen_n;

#define BAT_MAX_VERTS 10000u                         /* (its largest array, the colours, is 160 KB of a 256 KB chunk
                                                      * of the static store; smaller batches packed the chunks
                                                      * better but were three times as many draws) */
#define BAT_MAX_INDICES 30000u
#define BAT_SETTLE_FRAMES 60u

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
    float reg[4];
    if (tex != name)                                 /* an .ifl: its frame changes */
        return 0;
    if (p->uv && material_region(sm->b, p->material, reg))
        return 0;
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
    k->tex = texture(r, sm->b, tex);
    k->fmt = (uint8_t)((p->nrm ? 1 : 0) | (p->uv ? 2 : 0) | (p->col ? 4 : 0));
    k->two_sided = (material_flags(sm->b, p->material) & MAT_TWO_SIDED) != 0;
    k->clamp = k->tex && material_clamped(sm->b, p->material);
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
        if (key->fmt & 2)
            memcpy(b->uv + 2 * v0, p->uv, sizeof(float) * 2 * p->nverts);
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
static void bat_join(Renderer *r, uint32_t i, uint32_t k, const MeshPart *p)
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
            return;
        BatSub *n = realloc(bg->sub, sizeof *n * (size_t)(bg->nsub + 1));
        if (!n)
            return;
        bg->sub = n;
        memset(&n[bg->nsub], 0, sizeof *n);
        at = bg->nsub++;
    }
    BatSub *b = &bg->sub[at];
    if (b->nmem == b->cmem) {
        uint32_t c = b->cmem ? 2 * b->cmem : 16;
        BatMember *mm = realloc(b->mem, sizeof *mm * c);
        if (!mm)
            return;
        b->mem = mm;
        b->cmem = c;
    }
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
                    bi->out[k] = 0;
                    bi->key[k] = bat_sort_part(r, it, in, m, k, t);
                    if (bi->key[k] >= 0)
                        bat_join(r, i, k, &m->parts[k]);
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
            if (b->nidx && !bg->key.blend)
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
#endif

#ifdef RAMPAGE_XBOX
/* The quads of the effects and of the HUD, gathered and drawn as arrays: a glBegin .. glEnd for each one went through
 * gl4es's display-list machinery, a few dozen HUD quads costing as much as a hundred parts of the city. Quads are
 * added in their order and drawn when the state they need changes (the caller's qb_flush), so what is blended over
 * what is as before. */
#define QB_MAX 1024
static struct {
    float pos[QB_MAX * 4 * 3], uv[QB_MAX * 4 * 2], col[QB_MAX * 4 * 4];
    uint16_t idx[QB_MAX * 6];
    int n, ready;
} qb;

static void qb_flush(void)
{
    if (!qb.n)
        return;
    glVertexPointer(3, GL_FLOAT, 0, qb.pos);
    glTexCoordPointer(2, GL_FLOAT, 0, qb.uv);
    glColorPointer(4, GL_FLOAT, 0, qb.col);
    glDrawElements(GL_TRIANGLES, qb.n * 6, GL_UNSIGNED_SHORT, qb.idx);
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
/* the next quad's corner k: its place, texture coordinates and colour */
static void qb_vertex(int k, float x, float y, float z, float u, float v, const float rgb[3], float a)
{
    int at = qb.n * 4 + k;
    float *p = &qb.pos[3 * at], *t = &qb.uv[2 * at], *c = &qb.col[4 * at];
    p[0] = x; p[1] = y; p[2] = z;
    t[0] = u; t[1] = v;
    c[0] = rgb[0]; c[1] = rgb[1]; c[2] = rgb[2]; c[3] = a;
}
static void qb_next(void)
{
    if (++qb.n == QB_MAX)
        qb_flush();
}
#endif

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
    const float *cpos, *cnrm, *cuv, *ccol;           /* the client arrays */
} XboxDirect;

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
        gliSkinDraw(g->pos, g->nrm, g->uv, g->col, g->bone, g->weight, g->idx, (int)g->nidx,
                    (const float (*)[12])gp, g->nb);
    }
}

static void xbox_part_draw(Renderer *r, XboxDirect *d, GLsizei count, GLenum idx_type, const void *idx)
{
    if (d->sg) {
        if (gl4es_xbox_predraw())
            xbox_skin_draw(r, d->sg);
        else if (d->skin)                            /* its texture needs gl4es: the CPU poses it from now on */
            ((Skin *)d->skin)->gpu_skin = -1;
        return;
    }
    if (d->ok && gl4es_xbox_predraw()) {
        gliDirectDraw(d->pos, d->nrm, d->uv, d->col, d->idx16, count);
        return;
    }
    if (!d->arrays_set)
        xbox_client_arrays(d);
    glDrawElements(GL_TRIANGLES, count, idx_type, idx);
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
        g->bone = gliStaticArrayAlloc(sizeof(float) * 4 * ngv, sg);
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
        for (uint32_t q = 0; q < ngv; q++) {
            uint32_t v = gv[q];
            for (int k = 0; k < 4; k++) {
                float w = skin->weights[first + v][k];
                g->bone[4 * q + k] = w > 0 ? (float)local[skin->bones[first + v][k]] : 0.0f;
            }
        }
        for (uint32_t q = 0; q < ngv; q++) {
            uint32_t v = gv[q];
            for (int k = 0; k < 4; k++) {
                float w = skin->weights[first + v][k];
                g->weight[4 * q + k] = w > 0 ? w : 0.0f;
            }
        }
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
#ifdef RAMPAGE_XBOX
    const PartBuf *xp = xbox_part(r, sm, i, skin != NULL);
    XboxDirect xd;
    memset(&xd, 0, sizeof xd);
    xd.skin = skin;
    if (skin && skin->gpu_skin > 0 && !skin->cloth) {   /* posed by the GPU */
        xd.sg = r->cur_inst && xp ? xbox_skin_gpu(r, sm, skin, i, first, (PartBuf *)xp) : NULL;
        if (!xd.sg) {
            ((Skin *)skin)->gpu_skin = -1;           /* not this one: the CPU poses it from the next frame on */
            return;
        }
    } else if (skin && !skin->pos)
        return;                                      /* (the CPU has not posed it yet) */
#endif
    const Scene *s = r->scene;
    const MeshPart *p = &sm->mesh.parts[i];
    uint32_t tex = shown_texture(r, sm->b, material_texture(sm->b, p->material), t);
    float reg[4], rs[2] = { 1, 1 };                      /* a texture region: its tile (region_texture) */
    GLuint rid = p->uv && material_region(sm->b, p->material, reg) ? region_texture(r, sm->b, tex, reg, rs) : 0;
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
    if (scroll || rid) {                                 /* the drifting cloud band (AnimatingSkyData); the tile's
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
    int xbox_static = (xp && xp->vbo == 1) || (skin && skin->gpu);   /* (a posed mesh in the GPU's store) */
    if (xbox_static)
        gliStaticArraysHint(1);                      /* this part's draws, to the end of the function */
    GLenum idx_type = GL_UNSIGNED_INT;
    if (xp && xp->idx16) {
        idx = xp->idx16;
        idx_type = GL_UNSIGNED_SHORT;
        xd.idx16 = xp->idx16;
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
        sphere_coords(1);
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
    xbox_part_draw(r, &xd, (GLsizei)(3 * p->ntris), idx_type, idx);
#else
    glDrawElements(GL_TRIANGLES, (GLsizei)(3 * p->ntris), idx_type, idx);
#endif
    if (env_only) {
        sphere_coords(0);
#ifndef RAMPAGE_XBOX
        if (lit_was)
            glEnable(GL_LIGHTING);
#endif
    }
    if (test_was)
        glEnable(GL_ALPHA_TEST);
    if (rid) {                                           /* the region is texture slot 0's alone (0x265840) */
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
        sphere_coords(1);
        glDisable(GL_LIGHTING);
        glDisable(GL_FOG);
        glDisable(GL_ALPHA_TEST);
#ifdef RAMPAGE_XBOX
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
#else
        glDrawElements(GL_TRIANGLES, (GLsizei)(3 * p->ntris), idx_type, idx);
#endif
        sphere_coords(0);
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
static void draw_fx(Renderer *r, const FxQuad *fx, int nfx, const Camera *cam, float fog_start, float fog_end, int world)
{
    XS_END();
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
    qb_begin();
#endif
    for (int i = 0; i < nfx; i++) {
        const FxQuad *q = &fx[i];
        GLuint id = q->b ? texture_at(r, q->b, q->tex, 1) : 0;
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
            float cs = cosf(q->rot), sn = sinf(q->rot), rr[3], uu[3];
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
            a[k] = q->col[3] * f;
        }
        if (world && !q->ztest) {                          /* (above: tested in front of what it starts in) */
            float pull = 0;
            for (int k = 0; k < 4; k++) {
                float e[3] = { c[k][0] - q->pos[0], c[k][1] - q->pos[1], c[k][2] - q->pos[2] };
                pull = fmaxf(pull, sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]));
            }
            pull += 1;
            for (int k = 0; k < 4; k++) {
                float e[3] = { c[k][0] - cam->pos[0], c[k][1] - cam->pos[1], c[k][2] - cam->pos[2] };
                float d = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]), f = d > 0 ? fmaxf(d - pull, 0.5f) / d : 1;
                for (int j = 0; j < 3; j++)
                    c[k][j] = cam->pos[j] + e[j] * f;
            }
        }
        static const int UV[4][2] = { { 0, 1 }, { 2, 1 }, { 2, 3 }, { 0, 3 } };
#ifdef RAMPAGE_XBOX
        for (int k = 0; k < 4; k++)
            qb_vertex(k, c[k][0], c[k][1], c[k][2], q->uv[UV[k][0]], q->uv[UV[k][1]], q->col, a[k]);
        qb_next();
#else
        glBegin(GL_QUADS);
        for (int k = 0; k < 4; k++) {
            glColor4f(q->col[0], q->col[1], q->col[2], a[k]);
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
    glClearColor(sky[0], sky[1], sky[2], 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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
    if (s->sky >= 0) {
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
        }
#endif
        for (uint32_t i = 0; i < s->nitems; i++) {
            const DrawItem *it = &s->items[i];
            const Instance *in = &s->instances[it->instance];
            if (in->hidden || it->instance == s->sky || (it->instance == s->ground_inst) != !pass)
                continue;
#ifdef RAMPAGE_XBOX
            int seen = pass && i < g_xbox_item_seen_n ? g_xbox_item_seen[i] : item_seen(r, it, view_mv, tx, ty);
            if (i < g_xbox_item_seen_n)
                g_xbox_item_seen[i] = (uint8_t)seen;     /* (scene_update: a skin out of sight is not posed) */
            r->bat_keys = pass && i < r->nbitem && r->bitem[i].active ? r->bitem[i].key : NULL;
            r->cur_inst = in;
#else
            int seen = item_seen(r, it, view_mv, tx, ty);
#endif
            if (!seen)
                continue;
            int own = in->own_ambient && s->nlights;   /* an adornment's ambient (0x2CD060), the scene's after it */
            if (own)
                glLightModelfv(GL_LIGHT_MODEL_AMBIENT, (float[4]){ in->ambient[0], in->ambient[1], in->ambient[2], 1 });
            glPushMatrix();
            glMultMatrixf(it->world.m);
            draw_mesh(r, &s->meshes[it->mesh], it->skin, t);
            glPopMatrix();
            if (own)
                glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
        }
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
    if (s->nfx)                                            /* the effects' particles, over the world */
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
    if (s->nhud)
        draw_hud(r, s, w->width, w->height, t);
}
