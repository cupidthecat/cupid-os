#ifndef KERNEL_AUDIO_MIDIOPL_H
#define KERNEL_AUDIO_MIDIOPL_H

#include "types.h"

/* Parse GENMIDI lump and configure synth. Returns 0 on success. */
int  midiopl_init(const uint8_t *genmidi_lump, uint32_t lump_len);

/* Reset all OPL3 channels to silence. Keeps GENMIDI patches loaded. */
void midiopl_reset(void);

/* Push a MIDI byte stream into the synth. Running-status aware. */
void midiopl_feed(const uint8_t *bytes, uint32_t len);

/* Pull synth output: frames * 2 s16 stereo samples @ 22050 Hz.
 * Drives Nuked-OPL3 native @ 49716 Hz internally and resamples. */
void midiopl_render(int16_t *out_stereo, uint32_t frames);

void midiopl_set_volume(uint8_t vol_0_127);

#endif /* KERNEL_AUDIO_MIDIOPL_H */
