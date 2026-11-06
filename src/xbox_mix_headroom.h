/* Xbox port output protection. This is not claimed to reproduce the PS2's
 * SPU2 mixing hardware. Keep reconstructed voice/group gains intact and avoid
 * the float-to-s16 hard clip when many effects sum coherently.
 *
 * A stereo-linked, memoryless soft knee preserves samples below -2.50 dBFS
 * exactly. Above that, y = knee + span * excess / (span + excess), whose slope
 * is 1 at the knee and tends smoothly to zero. Both channels use the same gain,
 * retaining pan/phase. Unlike a block normalizer, this has no buffer-boundary
 * gain steps, lookahead latency, or state to reset on pause/restart.
 *
 * This deliberately compresses loud gameplay peaks, not ordinary-level audio.
 * Do not use for film PCM. A rational curve costs two divides only when above
 * the knee, and has no libm dependency on the original Xbox.
 */
#ifndef RAMPAGE_XBOX_MIX_HEADROOM_H
#define RAMPAGE_XBOX_MIX_HEADROOM_H
static void xbox_mix_headroom(float *pcm, int frames)
{
    const float knee = 24576.0f, ceiling = 32766.0f;
    const float span = ceiling - knee;
    for (int f = 0; f < frames; ++f) {
        float l = pcm[2 * f], r = pcm[2 * f + 1];
        float al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
        float peak = al > ar ? al : ar;
        if (peak > knee) {
            float excess = peak - knee;
            float limited = knee + span * (excess / (span + excess));
            float gain = limited / peak;
            pcm[2 * f] = l * gain;
            pcm[2 * f + 1] = r * gain;
        }
    }
}
#endif
