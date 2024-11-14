#include "pit.h"
#include "../kernel/ports.h"
#include "timer.h"

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

// PC speaker ports and bits
#define PC_SPEAKER_PORT 0x61
#define PC_SPEAKER_ENABLE_BITS 0x03
#define PC_SPEAKER_GATE_BIT 0x01
#define PC_SPEAKER_DATA_BIT 0x02

void pc_speaker_on(uint32_t frequency) {
    // Calculate PIT divisor for the desired frequency
    uint32_t divisor = 1193180 / frequency;
    
    // Configure PIT channel 2 for square wave generation
    outb(PIT_COMMAND, 0xB6);  // 10110110 - channel 2, square wave, both bytes
    outb(PIT_CHANNEL2, divisor & 0xFF);         // Low byte
    outb(PIT_CHANNEL2, (divisor >> 8) & 0xFF);  // High byte
    
    // Enable PC speaker by setting bits 0 and 1
    uint8_t tmp = inb(PC_SPEAKER_PORT);
    outb(PC_SPEAKER_PORT, tmp | PC_SPEAKER_ENABLE_BITS);
}

void pc_speaker_off(void) {
    // Disable PC speaker by clearing bits 0 and 1
    uint8_t tmp = inb(PC_SPEAKER_PORT);
    outb(PC_SPEAKER_PORT, tmp & ~PC_SPEAKER_ENABLE_BITS);
}

// Simple beep function that plays a 1kHz tone for 100ms
void beep(void) {
    // Initialize timer if not already done
    timer_init(100);  // Set up timer at 100Hz if not initialized
    
    pc_speaker_on(1000);  // 1kHz frequency
    
    // Use a more reliable delay
    for(volatile int i = 0; i < 1000000; i++) {
        __asm__ volatile("nop");
    }
    
    pc_speaker_off();
    
    // Additional delay between beeps
    for(volatile int i = 0; i < 500000; i++) {
        __asm__ volatile("nop");
    }
} 