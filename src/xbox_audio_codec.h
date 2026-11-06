/* Xemu currently models MCPX's codec with a generic STAC9700, whose reset
 * state is muted. The real Xbox's Wolfson WM9709 has no writable mixer volume
 * controls, so nxdk correctly does not normally touch these registers.
 * Restrict this compatibility change to the emulated codec identity, and
 * preserve attenuation while clearing only the two output mute bits. */
#ifndef RAMPAGE_XBOX_AUDIO_CODEC_H
#define RAMPAGE_XBOX_AUDIO_CODEC_H
#include <stdint.h>
static int xbox_audio_unmute_emulated_codec(volatile uint16_t *codec)
{
    if (codec[0x7c / 2] != 0x8384 || codec[0x7e / 2] != 0x7600)
        return 0;
    codec[0x02 / 2] = (uint16_t)(codec[0x02 / 2] & 0x7fff);
    codec[0x18 / 2] = (uint16_t)(codec[0x18 / 2] & 0x7fff);
    return 1;
}
#endif
