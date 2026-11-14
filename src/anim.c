/* anim.c - animation clips (bundle entry type 4, tools/anim.py, docs/FORMAT_BDP.md "Animations"), evaluated as the game
 * does it.
 * KeyInterpAnim (kind 2): 0x1C f32 duration, 0x2C u32 bones, 0x38 offset of the translation tracks, 0x3C of the rotation
 *   tracks. Translation track (0x191860): f32[3] scale, u8 bone, u8 keys, u8 keys - 1, u8 0, then per key u16 time,
 *   s16 x, y, z; value = s16 x scale x f32(0x38000100). Rotation track (0x191C50): u8 bone, u8 keys, then per key u16 time,
 *   s16 a, b, c, decoded by 0x192010. Key times are (time & 0xFFFE) / 65534 of the duration.
 * RawAnim (kind 1): per bone n translation segments (f32[3] p, f32[3] d: p + d f) and rotation segments (f32[4] q0,
 *   f32[4] q1, f32 a: normalise(q0 cos(a f) + q1 sin(a f)), or normalise(q0 + q1 f) when a is 0), spread evenly over the
 *   clip (0x19CEC0, 0x19D0A0, 0x167DE0).
 * Quaternions stay in the game's row-vector convention (mat_tq takes them so).
 * A KeyInterpAnim is sampled through a cursor per track, as the game's own Xbox build does it (0x274BE0, 0x274FB0,
 * docs/xbox_proto/CHARACTERS.md "Sampling with a cursor"): the key pair the time was last in, with its times and its
 * decoded values, so a frame inside the same pair searches and decodes nothing. What comes out is what the search
 * from key 0 gave. On the Xbox alone the slerp's arc is kept with the pair and its sines are series (no library call:
 * PDCLib's acosf, sinf and sqrtf are software); every other build keeps the exact sums, which are checked against
 * the PS2 game. */
#ifdef RAMPAGE_XBOX
#include <xmmintrin.h>
#endif
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "data.h"

static float f32bits(uint32_t u) { union { uint32_t u; float f; } v; v.u = u; return v.f; }

#ifdef RAMPAGE_XBOX
/* the console's own square root (sqrtss rounds as sqrtf must: the same result) */
static inline float root(float x) { return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x))); }
/* 1 / sqrt(x): rsqrtss's 12 bits and one Newton step, within 2e-7 of it */
static inline float inv_root(float x)
{
    float r = _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(x)));
    return r * (1.5f - 0.5f * x * r * r);
}
/* sin x and cos x for 0 .. pi/2, their series to x^9 and x^10: within 4e-6 and 5e-7 */
static inline float arc_sin(float x)
{
    float x2 = x * x;
    return x * (1 + x2 * (-1.0f / 6 + x2 * (1.0f / 120 + x2 * (-1.0f / 5040 + x2 * (1.0f / 362880)))));
}
static inline float arc_cos(float x)
{
    float x2 = x * x;
    return 1 + x2 * (-1.0f / 2 + x2 * (1.0f / 24 + x2 * (-1.0f / 720 + x2 * (1.0f / 40320 + x2 * (-1.0f / 3628800)))));
}
/* acos d for 0 .. 1 within 7e-5 (Abramowitz and Stegun 4.4.45); the slerp's weights are normalised after, which
 * leaves about a tenth of that in the pose */
static inline float arc_acos(float d)
{
    return root(1 - d) * (1.5707288f + d * (-0.2121144f + d * (0.0742610f + d * -0.0187293f)));
}
/* the slerp from a to b at f (0 .. 1) along a known arc: side is -1 where b is on the other side of a (the shortest
 * way goes to -b), angle the arc's, 0 for a pair so close that it lerps. out may be a */
static void arc_slerp(const float a[4], const float b[4], float side, float angle, float f, float out[4])
{
    float wa = 1 - f, wb = f, o[4], n = 0;
    if (angle != 0) {                                /* (their common 1 / sin(angle) goes in the normalising) */
        wa = arc_sin((1 - f) * angle);
        wb = arc_sin(f * angle);
    }
    wb *= side;
    for (int k = 0; k < 4; k++) { o[k] = a[k] * wa + b[k] * wb; n += o[k] * o[k]; }
    n = inv_root(n);
    for (int k = 0; k < 4; k++) out[k] = o[k] * n;
}
/* a pair's arc for arc_slerp: quat_slerp's own test for which pairs lerp */
static float arc_of(const float a[4], const float b[4], float *side)
{
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    *side = 1;
    if (d < 0) { d = -d; *side = -1; }
    return d < 0.9995f ? arc_acos(d) : 0;
}
#else
static inline float root(float x) { return sqrtf(x); }
#endif

/* 0x192010: three 13-bit components, w from the unit length plus a small correction, w's sign in the time's bit 0 */
static void decode_rotation(const uint8_t *key, float q[4])
{
    uint16_t time = rd16(key);
    int a = (int16_t)rd16(key + 2), b = (int16_t)rd16(key + 4), c = (int16_t)rd16(key + 6);
    float unit = f32bits(0x39800801), corr = f32bits(0x3B808081) * f32bits(0x39DD7E7C);
    float x = (float)(a >> 3) * unit, y = (float)(b >> 3) * unit, z = (float)(c >> 3) * unit;
    float s = x * x + y * y + z * z;
    if (s > 1) s = 1;
    int r = (a & 3) << 6 | (b & 7) << 3 | (c & 7);
    s += (float)((a & 4) ? -r : r) * corr;
    float w;
    if (s <= 1) {
        w = root(1 - s);
    } else {
        float n = root(x * x + y * y + z * z);
        x /= n; y /= n; z /= n; w = 0;
    }
    if (time & 1)
        w = -w;
    q[0] = x; q[1] = y; q[2] = z; q[3] = w;
}

/* a translation key's value: s16 x the track's scale x f32(0x38000100) */
static void decode_translation(const uint8_t *tr, const uint8_t *key, float v[3])
{
    float scale = f32bits(0x38000100);
    for (int i = 0; i < 3; i++) v[i] = (int16_t)rd16(key + 2 + 2 * i) * rdf(tr + 4 * i) * scale;
}

void quat_slerp(const float a[4], const float b[4], float f, float out[4])
{
#ifdef RAMPAGE_XBOX
    if (f >= 0 && f <= 1) {                          /* (the series hold over the arc only: beyond it, the library) */
        float side, angle = arc_of(a, b, &side);
        arc_slerp(a, b, side, angle, f, out);
        return;
    }
#endif
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3], bb[4] = { b[0], b[1], b[2], b[3] };
    if (d < 0) { d = -d; for (int k = 0; k < 4; k++) bb[k] = -bb[k]; }
    float wa = 1 - f, wb = f;
    if (d < 0.9995f) {
        float th = acosf(d), s = sinf(th);
        wa = sinf((1 - f) * th) / s;
        wb = sinf(f * th) / s;
    }
    float n = 0;
    for (int k = 0; k < 4; k++) { out[k] = a[k] * wa + bb[k] * wb; n += out[k] * out[k]; }
    n = sqrtf(n);
    for (int k = 0; k < 4; k++) out[k] /= n;
}

/* A track's cursor: the pair's first key k, its times as the search divides them (u16 even part / 65534) and its two
 * keys decoded: b is the next key's, or a again at the track's last key. Every bone has the rotation's; the
 * translation's is only for a track of two keys or more (slot ti of the block's trans), most having one or none. */
enum { CUR_SET = 1,                                  /* the pair below is set */
       CUR_SEEN = 2, CUR_ORDERED = 4,                /* the track's key times were looked at; they never go back */
       CUR_FLIP = 8 };                               /* Xbox: b is on the other side of a (arc_of's side -1) */
#define NO_SLOT 0xFFFF
typedef struct {
    uint8_t flags, k;
    uint16_t ti;
    float t[2], a[4], b[4];
#ifdef RAMPAGE_XBOX
    float angle;                                     /* the pair's arc (arc_of) */
#endif
} RotCursor;
typedef struct {
    uint8_t flags, k;
    float t[2], a[3], b[3];
} TransCursor;

/* A clip's cursors: made when the clip is first sampled, not when it is read (a monster has some 145 clips read
 * and plays one or two), and within CURSOR_BUDGET for all the clips together: the block longest unused goes to make
 * room. A clip that lost its block, or could not get one, is searched from key 0, which gives the same
 * pose, and makes a block again the next time. The blocks are known by the clip's cell (Clip.cursor), which is on
 * the heap with its tracks and so stays put while the Clip is moved or copied. */
struct ClipCursor {
    struct ClipCursor *prev, *next;                  /* cursor_list */
    struct ClipCursor **cell;                        /* its clip's, which points here */
    uint32_t bytes, used;                            /* used: cursor_clock when last sampled */
    int asked; float tn; int until;                  /* the last time asked for, and the last key time not after it */
    TransCursor *trans;
    RotCursor bone[];
};
#ifdef RAMPAGE_XBOX
#define CURSOR_BUDGET ((size_t)512 * 1024)           /* of the console's 64 MB: some 160 monster clips' worth */
#else
#define CURSOR_BUDGET SIZE_MAX                       /* no limit */
#endif
static struct ClipCursor *cursor_list;
static size_t cursor_bytes;                          /* of all the blocks in the list */
static uint32_t cursor_clock;                        /* one tick a sample */

static void cursor_drop(struct ClipCursor *cc)
{
    if (cc->prev) cc->prev->next = cc->next; else cursor_list = cc->next;
    if (cc->next) cc->next->prev = cc->prev;
    cursor_bytes -= cc->bytes;
    *cc->cell = NULL;
    free(cc);
}

/* a block for a clip that has none (so the clip being sampled is never the one that makes room); NULL: none now */
static struct ClipCursor *cursor_make(const Clip *c)
{
    if (c->bones >= NO_SLOT)
        return NULL;
    uint32_t slots = 0;
    for (uint32_t i = 0; i < c->bones; i++)
        if (c->trans[i][0xD] > 1) slots++;
    size_t bytes = sizeof(struct ClipCursor) + c->bones * sizeof(RotCursor) + slots * sizeof(TransCursor);
    while (bytes > CURSOR_BUDGET - cursor_bytes) {   /* no room: the block longest unused goes */
        struct ClipCursor *old = cursor_list;
        if (!old)
            return NULL;
        for (struct ClipCursor *p = old->next; p; p = p->next)
            if (cursor_clock - p->used > cursor_clock - old->used) old = p;
        cursor_drop(old);
    }
    struct ClipCursor *cc = calloc(1, bytes);        /* all clear: every track is searched from its start once */
    if (!cc)
        return NULL;
    cc->trans = (TransCursor *)(void *)(cc->bone + c->bones);
    slots = 0;
    for (uint32_t i = 0; i < c->bones; i++)
        cc->bone[i].ti = (uint16_t)(c->trans[i][0xD] > 1 ? slots++ : NO_SLOT);
    cc->bytes = (uint32_t)bytes;
    cc->cell = c->cursor;
    cc->next = cursor_list;
    if (cursor_list) cursor_list->prev = cc;
    cursor_list = cc;
    cursor_bytes += bytes;
    *c->cursor = cc;
    return cc;
}

/* The last key time (0 .. 65534) not after tn, -1 before them all: u / 65534.0f <= tn exactly when u <= this (the
 * quotient never falls as u grows), so the searches compare the stored times as they are and divide nothing. */
static int time_until(float tn)
{
    if (!(tn >= 0))
        return -1;
    if (tn >= 1)
        return 65534;
    int u = (int)(tn * 65534.0f);
    while (u < 65534 && (u + 1) / 65534.0f <= tn) u++;   /* the product is rounded: the division itself settles it */
    while (u > 0 && u / 65534.0f > tn) u--;
    return u;
}

static int keys_ordered(const uint8_t *keys, uint32_t n)
{
    for (uint32_t k = 1; k < n; k++)
        if ((rd16(keys + 8 * k) & 0xFFFE) < (rd16(keys + 8 * (k - 1)) & 0xFFFE))
            return 0;
    return 1;
}

/* The key a time is in, as the search from key 0 finds it (the first key whose next one is after the time), started
 * at k: the key it was in last for a track whose times never go back, where that is the same key (and from the start
 * when the time went back, as the game's cursors do); 0 for any other track. */
static uint32_t key_at(const uint8_t *keys, uint32_t n, uint32_t k, int until)
{
    if (k && (int)(rd16(keys + 8 * k) & 0xFFFE) > until)
        k = 0;
    while (k + 1 < n && (int)(rd16(keys + 8 * (k + 1)) & 0xFFFE) <= until) k++;
    return k;
}

/* a KeyInterpAnim's bone at tn (0 .. 1 of the clip): the tracks without keys leave t / q as they came */
static void pose_keys(const Clip *c, float tn, uint32_t bone, float t[3], float q[4])
{
    struct ClipCursor *cc = c->cursor ? *c->cursor : NULL;
    if (!cc && c->cursor)
        cc = cursor_make(c);
    RotCursor rnone, *rc = &rnone;                   /* without a block the cursors are these two, set to nothing: */
    TransCursor tnone;                               /* the search from key 0 and both keys decoded, kept nowhere */
    int until;
    if (cc) {
        cc->used = ++cursor_clock;
        if (!cc->asked || cc->tn != tn) {            /* (every bone of a pose asks for the same time) */
            cc->until = time_until(tn);
            cc->tn = tn;
            cc->asked = 1;
        }
        until = cc->until;
        rc = &cc->bone[bone];
    } else {
        until = time_until(tn);
        rnone.flags = CUR_SEEN;
        rnone.k = 0;
        rnone.ti = NO_SLOT;
    }
    const uint8_t *tr = c->trans[bone], *keys = tr + 0x10;
    uint32_t n = tr[0xD];
    if (n) {
        TransCursor *tc = &tnone;
        if (rc->ti != NO_SLOT) {
            tc = &cc->trans[rc->ti];
        } else {                                     /* (a track of one key too: its value decoded as it comes) */
            tnone.flags = CUR_SEEN;
            tnone.k = 0;
        }
        if (!(tc->flags & CUR_SEEN))
            tc->flags |= keys_ordered(keys, n) ? CUR_SEEN | CUR_ORDERED : CUR_SEEN;
        uint32_t k = key_at(keys, n, tc->flags & CUR_ORDERED ? tc->k : 0, until);
        if (!(tc->flags & CUR_SET) || k != tc->k) {
            if ((tc->flags & CUR_SET) && k == tc->k + 1u)
                memcpy(tc->a, tc->b, sizeof tc->a);  /* one key on: the last pair's second key is this one's first */
            else
                decode_translation(tr, keys + 8 * k, tc->a);
            if (k + 1 < n) {
                tc->t[0] = (rd16(keys + 8 * k) & 0xFFFE) / 65534.0f;
                tc->t[1] = (rd16(keys + 8 * (k + 1)) & 0xFFFE) / 65534.0f;
                decode_translation(tr, keys + 8 * (k + 1), tc->b);
            } else {
                memcpy(tc->b, tc->a, sizeof tc->b);
            }
            tc->k = (uint8_t)k;
            tc->flags |= CUR_SET;
        }
        float fr = 0;
        if (k + 1 < n) {
            fr = tc->t[1] > tc->t[0] ? (tn - tc->t[0]) / (tc->t[1] - tc->t[0]) : 0;
            if (fr < 0) fr = 0;
        }
        for (int i = 0; i < 3; i++) t[i] = tc->a[i] + (tc->b[i] - tc->a[i]) * fr;
    }
    const uint8_t *rt = c->rot[bone], *rk = rt + 2;
    n = rt[1];
    if (n) {
        if (!(rc->flags & CUR_SEEN))
            rc->flags |= keys_ordered(rk, n) ? CUR_SEEN | CUR_ORDERED : CUR_SEEN;
        uint32_t k = key_at(rk, n, rc->flags & CUR_ORDERED ? rc->k : 0, until);
        if (!(rc->flags & CUR_SET) || k != rc->k) {  /* a key is decoded (its square root) when the pair moves on */
            if ((rc->flags & CUR_SET) && k == rc->k + 1u)
                memcpy(rc->a, rc->b, sizeof rc->a);
            else
                decode_rotation(rk + 8 * k, rc->a);
            if (k + 1 < n) {
                rc->t[0] = (rd16(rk + 8 * k) & 0xFFFE) / 65534.0f;
                rc->t[1] = (rd16(rk + 8 * (k + 1)) & 0xFFFE) / 65534.0f;
                decode_rotation(rk + 8 * (k + 1), rc->b);
#ifdef RAMPAGE_XBOX
                float side;
                rc->angle = arc_of(rc->a, rc->b, &side);
                rc->flags = (uint8_t)(side < 0 ? rc->flags | CUR_FLIP : rc->flags & ~CUR_FLIP);
#endif
            } else {
                memcpy(rc->b, rc->a, sizeof rc->b);
            }
            rc->k = (uint8_t)k;
            rc->flags |= CUR_SET;
        }
        if (k + 1 < n) {
            float fr = rc->t[1] > rc->t[0] ? (tn - rc->t[0]) / (rc->t[1] - rc->t[0]) : 0;
            if (fr < 0) fr = 0;
#ifdef RAMPAGE_XBOX
            if (fr <= 1)                             /* (the next key is after tn: always, but for a time that is
                                                      * no number) */
                arc_slerp(rc->a, rc->b, rc->flags & CUR_FLIP ? -1.0f : 1.0f, rc->angle, fr, q);
            else
#endif
            quat_slerp(rc->a, rc->b, fr, q);
        } else {
            memcpy(q, rc->a, sizeof rc->a);
        }
    }
}

int clip_read(const BdpEntry *e, Clip *c)
{
    memset(c, 0, sizeof *c);
    if (!e || e->type != ET_ANIM || e->size < 0x44)
        return -1;
    const uint8_t *d = e->data;
    c->data = d;
    c->kind = rd32(d + 0x20);
    c->duration = rdf(d + 0x1C);
    c->skeleton = rd16(d + 0x28);
    if (c->kind == 2) {
        c->bones = rd32(d + 0x2C);
        c->trans = calloc(c->bones ? c->bones : 1, sizeof *c->trans);
        if (!c->trans)
            goto fail;
        /* the cursors' cell goes after the rotation tracks, in their block: where a moved or copied Clip still finds
         * it, and no cursor is made here */
        size_t nrot = c->bones ? c->bones : 1;
        if (nrot > (SIZE_MAX - sizeof *c->cursor) / sizeof *c->rot)
            goto fail;
        c->rot = calloc(1, nrot * sizeof *c->rot + sizeof *c->cursor);
        if (!c->rot)
            goto fail;
        c->cursor = (struct ClipCursor **)(void *)(c->rot + nrot);
        *c->cursor = NULL;
        const uint8_t *p = d + rd32(d + 0x38);
        for (uint32_t i = 0; i < c->bones; i++) {
            c->trans[i] = p;
            p += 0x10 + 8 * p[0xD];
        }
        p = d + rd32(d + 0x3C);
        for (uint32_t i = 0; i < c->bones; i++) {
            c->rot[i] = p;
            p += 2 + 8 * p[1];
        }
    } else if (c->kind == 1) {
        uint32_t count = rd32(d + 0x2C), table = rd32(d + 0x30);
        for (uint32_t i = 0; i < count; i++) {
            uint32_t bone = rd32(d + rd32(d + table + 4 * i)) + 1;
            if (bone > c->bones) c->bones = bone;
        }
        c->trans = calloc(c->bones ? c->bones : 1, sizeof *c->trans);   /* per bone: its record */
        if (!c->trans)
            goto fail;
        for (uint32_t i = 0; i < count; i++) {
            const uint8_t *rec = d + rd32(d + table + 4 * i);
            c->trans[rd32(rec)] = rec;
        }
    } else {
        goto fail;
    }
    return 0;
fail:
    clip_free(c);
    return -1;
}

void clip_free(Clip *c)
{
    if (c->cursor && *c->cursor)
        cursor_drop(*c->cursor);                     /* (out of the list; the cell itself goes with rot) */
    free((void *)c->trans);
    free((void *)c->rot);
    memset(c, 0, sizeof *c);
}

int clip_moves(const Clip *c)
{
    if (c->kind == 2)
        return 1;
    for (uint32_t i = 0; i < c->bones; i++) {
        const uint8_t *rec = c->trans[i];
        if (!rec) continue;
        uint32_t nt = rd32(rec + 8), nr = rd32(rec + 12);
        const uint8_t *tp = rec + rd32(rec + 0x1C), *rp = rec + rd32(rec + 0x20);
        float q0[4];
        for (int k = 0; k < 4; k++) q0[k] = rdf(rp + 4 * k);
        for (uint32_t s = 0; s < nt; s++)
            for (int k = 0; k < 3; k++)
                if (fabsf(rdf(tp + 24 * s + 12 + 4 * k)) > 1e-5f || fabsf(rdf(tp + 24 * s + 4 * k) - rdf(tp + 4 * k)) > 1e-5f)
                    return 1;
        for (uint32_t s = 0; s < nr; s++) {
            if (rdf(rp + 36 * s + 32) != 0) return 1;
            for (int k = 0; k < 4; k++)
                if (fabsf(rdf(rp + 36 * s + 16 + 4 * k)) > 1e-5f || fabsf(rdf(rp + 36 * s + 4 * k) - q0[k]) > 1e-5f)
                    return 1;
        }
    }
    return 0;
}

double clip_phase(const Clip *c, double seconds)
{
#ifdef RAMPAGE_XBOX
    double x = seconds / c->duration;
    if (x > -2e9 && x < 2e9) {                       /* fmod(x, 1.0) is x less its whole part, with no library call */
        double f = x - (double)(int)x;
        if (f < 0) f += 1;
        return f;
    }
#endif
    double f = fmod(seconds / c->duration, 1.0);
    if (f < 0) f += 1;
    return f;
}

int clip_pose_phase(const Clip *c, double f, uint32_t bone, float t[3], float q[4])
{
    if (bone >= c->bones || c->duration <= 0)
        return 0;
    if (c->kind == 2) {
        pose_keys(c, (float)f, bone, t, q);
        return 1;
    }
    const uint8_t *rec = c->trans[bone];
    if (!rec)
        return 0;
    uint32_t nt = rd32(rec + 8), nr = rd32(rec + 12);
    const uint8_t *tp = rec + rd32(rec + 0x1C), *rp = rec + rd32(rec + 0x20);
    double x = f * nt;
    uint32_t k = (uint32_t)x < nt ? (uint32_t)x : nt - 1;
    float fr = (float)(x - k);
    for (int i = 0; i < 3; i++) t[i] = rdf(tp + 24 * k + 4 * i) + rdf(tp + 24 * k + 12 + 4 * i) * fr;
    x = f * nr;
    k = (uint32_t)x < nr ? (uint32_t)x : nr - 1;
    fr = (float)(x - k);
    const uint8_t *s = rp + 36 * k;
    float angle = rdf(s + 32), n = 0;
#ifdef RAMPAGE_XBOX
    float turn = angle * fr, cosine = 0, sine = 0;
    if (turn >= 0 && turn <= 1.5707963f) {           /* within the series: no library call */
        cosine = arc_cos(turn);
        sine = arc_sin(turn);
    } else if (angle != 0) {
        cosine = cosf(turn);
        sine = sinf(turn);
    }
    for (int i = 0; i < 4; i++) {
        float q0 = rdf(s + 4 * i), q1 = rdf(s + 16 + 4 * i);
        q[i] = angle == 0 ? q0 + q1 * fr : q0 * cosine + q1 * sine;
        n += q[i] * q[i];
    }
    n = inv_root(n);
    for (int i = 0; i < 4; i++) q[i] *= n;
#else
    float cosine = angle == 0 ? 0 : cosf(angle * fr), sine = angle == 0 ? 0 : sinf(angle * fr);
    for (int i = 0; i < 4; i++) {
        float q0 = rdf(s + 4 * i), q1 = rdf(s + 16 + 4 * i);
        q[i] = angle == 0 ? q0 + q1 * fr : q0 * cosine + q1 * sine;
        n += q[i] * q[i];
    }
    n = sqrtf(n);
    for (int i = 0; i < 4; i++) q[i] /= n;
#endif
    return 1;
}

int clip_pose(const Clip *c, double seconds, uint32_t bone, float t[3], float q[4])
{
    if (bone >= c->bones || c->duration <= 0)
        return 0;
    return clip_pose_phase(c, clip_phase(c, seconds), bone, t, q);
}
