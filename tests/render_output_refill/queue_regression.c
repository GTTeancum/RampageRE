/* Included after current sound.c and exact Renderer/helper definitions. */
#include <assert.h>

static unsigned queued, hardware_frames;
static int paused;
static double render_tick;
static Renderer *renderer;

void SDL_PauseAudioDevice(SDL_AudioDeviceID device, int value)
{
    (void)device;
    paused = value;
}

unsigned SDL_GetQueuedAudioSize(SDL_AudioDeviceID device)
{
    (void)device;
    return queued;
}

int SDL_QueueAudio(SDL_AudioDeviceID device, const void *pcm, unsigned bytes)
{
    (void)device;
    (void)pcm;
    queued += bytes;
    return 0;
}

void SDL_ClearQueuedAudio(SDL_AudioDeviceID device)
{
    (void)device;
    queued = 0;
}

unsigned rampage_audio_pending_frames(void) { return hardware_frames; }
const char *SDL_GetError(void) { return "test"; }
double win_time(void) { return render_tick; }

static int pull(void *ctx, int16_t *pcm, int frames)
{
    (void)ctx;
    memset(pcm, 0, sizeof *pcm * 2 * (size_t)frames);
    return frames;
}

static void consume(unsigned frames)
{
    unsigned total = queued / 4 + hardware_frames;
    assert(total >= frames);
    total -= frames;
    hardware_frames = total > 1024 ? 1024 : total;
    queued = (total - hardware_frames) * 4;
}

static void render_service(void)
{
    /* Make this an eligible callback checkpoint. Queue consumption is modeled
     * independently below, not inferred from this synthetic clock. */
    render_tick += .020;
    pump_output(renderer);
}

int main(void)
{
    renderer = rnd_create();
    assert(renderer);
    rnd_set_output_pump(renderer, sound_pump);
    g_dev = 1;
    g_emulated_audio = 1;
    S.ready = 1;
    S.sfx_gain = S.music_gain = 1;
    S.now = 12345;
    S.ms_acc = .625;

    int16_t pcm[64];
    for (int i = 0; i < 64; ++i)
        pcm[i] = (int16_t)(i * 100);
    Sample sample = {0};
    sample.pcm = pcm;
    sample.n = 64;
    sample.loop = 0;
    S.voice[0].on = 1;
    S.voice[0].s = &sample;
    S.voice[0].step = 1;
    S.voice[0].l = S.voice[0].r = 8192;
    S.inst[0].gain = 1;
    S.rng = 0x12345678;
    S.next_handle = 42;
    S.lis_pos[0] = 1.25f;
    S.lis_pos[2] = -99;
    unsigned char instances[sizeof S.inst], tracks[sizeof S.track];
    unsigned char listener[sizeof S.lis_pos];
    memcpy(instances, S.inst, sizeof instances);
    memcpy(tracks, S.track, sizeof tracks);
    memcpy(listener, S.lis_pos, sizeof listener);

    render_service();
    assert(queued == 8 * 4096);
    unsigned first = queued;
    render_service();
    render_service();
    assert(queued == first);                    /* Full queue is not duplicated. */
    for (int i = 0; i < 100; ++i) {
        /* 80ms + 110ms + 50ms exceeds 171ms in total, but each phase fits.
         * This reproduces the earlier phased refill fixture, not a GPU model. */
        const unsigned stages[] = {3840, 5280, 2400};
        for (int k = 0; k < 3; ++k) {
            consume(stages[k]);
            render_service();
            assert(queued >= 8 * 4096 && queued < 9 * 4096);
        }
        assert(S.now == 12345 && S.ms_acc == .625);
        assert(!memcmp(instances, S.inst, sizeof instances));
        assert(!memcmp(tracks, S.track, sizeof tracks));
        assert(!memcmp(listener, S.lis_pos, sizeof listener));
        assert(S.rng == 0x12345678 && S.next_handle == 42 && !S.manual_mix);
        assert(S.voice[0].on);
    }

    g_emulated_audio = 0;
    SDL_ClearQueuedAudio(g_dev);
    hardware_frames = 0;
    render_service();
    assert(queued == 5 * 4096);                  /* Native game's 107ms target. */
    S.ready = 0;
    SDL_ClearQueuedAudio(g_dev);
    render_service();
    assert(!queued);
    S.ready = 1;
    g_dev = 0;
    render_service();
    assert(!queued);                           /* Closed output remains inert. */

    g_dev = 1;
    S.pull = pull;
    render_service();
    assert(queued == 5 * 4096);                  /* Native movie queue. */
    SDL_ClearQueuedAudio(g_dev);
    hardware_frames = 0;
    g_emulated_audio = 1;
    render_service();
    assert(queued == 24 * 4096);                 /* Emulated movie queue. */
    assert(S.now == 12345 && S.ms_acc == .625 && !S.manual_mix);
    assert(!paused);
    free(renderer);
    puts("PASS renderer-to-sound: phased 240ms refill, bounded/idempotent queues, "
         "unchanged event/listener state, native/emulated targets, unavailable backend");
    return 0;
}

/* Music decoding can reach this boundary; the fixture uses no asset reads. */
int vol_read_range(const Vol *volume, const VolFile *file, uint32_t offset,
                   void *data, size_t size)
{
    (void)volume;
    (void)file;
    (void)offset;
    (void)data;
    (void)size;
    return -1;
}
