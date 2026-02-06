#ifndef PIT_H
#define PIT_H

#include "../kernel/types.h"

// PIT ports
#define PIT_CHANNEL0 0x40
#define PIT_CHANNEL1 0x41
#define PIT_CHANNEL2 0x42
#define PIT_COMMAND  0x43

// PIT command bits
#define PIT_CHANNEL0_SELECT 0x00
#define PIT_CHANNEL1_SELECT 0x40
#define PIT_CHANNEL2_SELECT 0x80
#define PIT_LOBYTE_HIBYTE   0x30
#define PIT_SQUARE_WAVE     0x06

// Function declarations
void pit_init(uint32_t channel, uint32_t frequency);
void pit_set_frequency(uint32_t channel, uint32_t frequency);

// Configure PIT channel 0 for 100Hz scheduler ticks (10ms time slices)
void pit_set_scheduler_mode(void);

#endif 