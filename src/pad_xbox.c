/* Original Xbox input through nxdk's SDL 2.0.9-compatible controller API.
 * Black/white are SDL right/left shoulder; triggers stay analog.
 * Avoid modern SDL controller type, light and touch APIs absent in nxdk. */
#ifdef RAMPAGE_XBOX
#include "pad.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EFFECTS 8
static struct {
    SDL_GameController *c;
    SDL_JoystickID id;
    float strength[EFFECTS];
    double until[EFFECTS];
    unsigned sent;
    double sent_at;
} P[PAD_SLOTS];
static int initialized;
static float rumble_scale = 1.0f;

static double now_seconds(void)
{
    Uint64 freq = SDL_GetPerformanceFrequency();
    return freq ? (double)SDL_GetPerformanceCounter() / (double)freq : (double)SDL_GetTicks() / 1000.0;
}
static float unit(float v) { return !(v > 0) ? 0 : v > 1 ? 1 : v; }
static float axis(SDL_GameController *c, SDL_GameControllerAxis a)
{
    int n = SDL_GameControllerGetAxis(c, a);
    return n < 0 ? (float)n / 32768.0f : (float)n / 32767.0f;
}
const char *pad_kind_name(int kind) { return kind == PADK_XBOX ? "Xbox controller" : "none"; }
void pad_set_strength(float s) { rumble_scale = unit(s); }
void pad_rumble(int slot, float strength, float seconds)
{
    if (slot < 0 || slot >= PAD_SLOTS || !P[slot].c || !(seconds > 0)) return;
    int j = 0;
    for (int i = 1; i < EFFECTS; ++i) if (P[slot].until[i] < P[slot].until[j]) j = i;
    P[slot].strength[j] = unit(strength);
    P[slot].until[j] = now_seconds() + seconds;
}
static void send_rumble(int k, unsigned value, double now)
{
    if (value == P[k].sent && (!value || now - P[k].sent_at < 0.5)) return;
    if (SDL_GameControllerRumble(P[k].c, (Uint16)value, 0, value ? 1000 : 0) == 0) {
        P[k].sent = value;
        P[k].sent_at = now;
    }
}
void pad_close(void)
{
    for (int k = 0; k < PAD_SLOTS; ++k) if (P[k].c) {
        SDL_GameControllerRumble(P[k].c, 0, 0, 0);
        SDL_GameControllerClose(P[k].c);
    }
    memset(P, 0, sizeof P);
    if (initialized == 1) SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    initialized = 0;
}
static void reconcile(void)
{
    for (int k = 0; k < PAD_SLOTS; ++k) if (P[k].c && !SDL_GameControllerGetAttached(P[k].c)) {
        SDL_GameControllerClose(P[k].c);
        memset(&P[k], 0, sizeof P[k]);
    }
    for (int d = 0; d < SDL_NumJoysticks(); ++d) {
        if (!SDL_IsGameController(d)) continue;
        SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(d);
        int have = 0, slot = -1;
        if (id < 0) continue;
        for (int k = 0; k < PAD_SLOTS; ++k) {
            if (P[k].c && P[k].id == id) have = 1;
            if (!P[k].c && slot < 0) slot = k;
        }
        if (have || slot < 0) continue;
        SDL_GameController *c = SDL_GameControllerOpen(d);
        if (!c) continue;
        P[slot].c = c;
        P[slot].id = id;
        printf("pad: Xbox controller %ld assigned to player %d\n", (long)id, slot + 1);
    }
}
unsigned pad_poll(PadState st[PAD_SLOTS], int focused, unsigned devchanges)
{
    static const struct { SDL_GameControllerButton from; int to; } map[] = {
        {SDL_CONTROLLER_BUTTON_A, PAD_A}, {SDL_CONTROLLER_BUTTON_B, PAD_B},
        {SDL_CONTROLLER_BUTTON_X, PAD_X}, {SDL_CONTROLLER_BUTTON_Y, PAD_Y},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, PAD_LB}, /* white */
        {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, PAD_RB}, /* black */
        {SDL_CONTROLLER_BUTTON_BACK, PAD_BACK}, {SDL_CONTROLLER_BUTTON_START, PAD_START},
        {SDL_CONTROLLER_BUTTON_LEFTSTICK, PAD_LS}, {SDL_CONTROLLER_BUTTON_RIGHTSTICK, PAD_RS},
        {SDL_CONTROLLER_BUTTON_DPAD_UP, PAD_UP}, {SDL_CONTROLLER_BUTTON_DPAD_DOWN, PAD_DOWN},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT, PAD_LEFT}, {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, PAD_RIGHT}
    };
    (void)devchanges;
    memset(st, 0, sizeof *st * PAD_SLOTS);
    if (!initialized) {
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
            fprintf(stderr, "pad: %s\n", SDL_GetError());
            initialized = -1;
            return 0;
        }
        initialized = 1;
        atexit(pad_close);
    }
    if (initialized != 1) return 0;
    SDL_GameControllerUpdate();
    reconcile();
    double now = now_seconds();
    unsigned mask = 0;
    for (int k = 0; k < PAD_SLOTS; ++k) if (P[k].c) {
        PadState *s = &st[k];
        mask |= 1u << k;
        s->kind = PADK_XBOX;
        float rumble = 0;
        if (focused) {
            for (unsigned i = 0; i < sizeof map / sizeof *map; ++i)
                if (SDL_GameControllerGetButton(P[k].c, map[i].from)) s->buttons |= 1u << map[i].to;
            s->lx = axis(P[k].c, SDL_CONTROLLER_AXIS_LEFTX); s->ly = axis(P[k].c, SDL_CONTROLLER_AXIS_LEFTY);
            s->rx = axis(P[k].c, SDL_CONTROLLER_AXIS_RIGHTX); s->ry = axis(P[k].c, SDL_CONTROLLER_AXIS_RIGHTY);
            s->lt = unit(axis(P[k].c, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
            s->rt = unit(axis(P[k].c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));
            if (s->lt >= 0.25f) s->buttons |= 1u << PAD_LT;
            if (s->rt >= 0.25f) s->buttons |= 1u << PAD_RT;
            for (int i = 0; i < EFFECTS; ++i)
                if (P[k].until[i] > now && P[k].strength[i] > rumble) rumble = P[k].strength[i];
        } else {
            /* Do not replay a paused effect when focus returns. */
            memset(P[k].until, 0, sizeof P[k].until);
        }
        send_rumble(k, (unsigned)(unit(rumble * rumble_scale) * 65535.0f + 0.5f), now);
    }
    return mask;
}
#endif
