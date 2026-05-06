#ifndef KERNEL_AUDIO_AC97_H
#define KERNEL_AUDIO_AC97_H

#include "../types.h"

/* ac97_init — probe PCI bus, configure codec, set up BDL ring,
 * install IRQ handler. Returns 0 on success, negative on failure
 * (no device / unsupported codec). Audio runs silent on failure;
 * caller does not have to abort.
 */
int  ac97_init(void);

/* Set fill callback. Called from the AC97 IRQ tail to refill
 * the buffer that just emptied. Buffer is s16-stereo @ 22050 Hz,
 * `frames` is the per-buffer frame count (1024 frames).
 *
 * Callback runs under BKL with IRQs disabled. Keep it bounded.
 */
void ac97_set_fill_callback(void (*fill)(int16_t *buf, uint32_t frames));

void ac97_start(void);            /* arm DMA */
void ac97_stop(void);             /* mute + halt DMA */
void ac97_set_master_volume(uint8_t pct);  /* 0-100 */
bool ac97_is_present(void);

#endif
