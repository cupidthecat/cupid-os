#ifndef KERNEL_AUDIO_MIXER_H
#define KERNEL_AUDIO_MIXER_H

#include "../types.h"

#define MIXER_SLOTS         16
#define MIXER_RATE_HZ       22050

typedef void (*mixer_pull_fn)(int16_t *out_stereo, uint32_t frames, void *ctx);

int  mixer_init(void);

int  mixer_play(int slot, const int16_t *pcm, uint32_t frames,
                uint8_t channels, uint8_t loop,
                uint8_t vol_l, uint8_t vol_r);

int  mixer_play_stream(int slot, mixer_pull_fn pull, void *ctx,
                       uint8_t vol_l, uint8_t vol_r);

void mixer_stop(int slot);
int  mixer_active(int slot);
void mixer_set_volume(int slot, uint8_t vol_l, uint8_t vol_r);

void mixer_fill(int16_t *out, uint32_t frames);

#endif
