#ifndef SPEAKER_H
#define SPEAKER_H

#include "../kernel/types.h"

// PC Speaker port definitions
#define SPEAKER_PORT 0x61
#define PIT_CHANNEL2 0x42
#define PIT_COMMAND 0x43

// Function declarations
// Turn on PC speaker at specified frequency
void pc_speaker_on(uint32_t frequency);

// Turn off PC speaker
void pc_speaker_off(void);

// Play a simple beep sound
void beep(void);

// test speaker
void test_speaker(void);

#endif