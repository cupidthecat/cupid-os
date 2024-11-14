#include "pit.h"
#include "../kernel/ports.h"

void pit_init(uint32_t channel, uint32_t frequency) {
    pit_set_frequency(channel, frequency);
}

void pit_set_frequency(uint32_t channel, uint32_t frequency) {
    uint32_t divisor = 1193182 / frequency;
    uint8_t command = PIT_LOBYTE_HIBYTE | PIT_SQUARE_WAVE;

    switch (channel) {
        case 0:
            command |= PIT_CHANNEL0_SELECT;
            break;
        case 1:
            command |= PIT_CHANNEL1_SELECT;
            break;
        case 2:
            command |= PIT_CHANNEL2_SELECT;
            break;
        default:
            return; // Invalid channel
    }

    outb(PIT_COMMAND, command);
    outb(PIT_CHANNEL0 + channel, divisor & 0xFF);         // Low byte
    outb(PIT_CHANNEL0 + channel, (divisor >> 8) & 0xFF);  // High byte
} 