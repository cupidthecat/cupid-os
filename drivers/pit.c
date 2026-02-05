/*
 * Programmable Interval Timer (PIT) Driver Implementation
 *
 * This file implements the Intel 8253/8254 PIT configuration and control:
 * - Supports all 3 PIT channels (0-2):
 *   - Channel 0: System timer (default 100Hz)
 *   - Channel 1: DRAM refresh (legacy)
 *   - Channel 2: PC speaker control
 * - Configurable frequency per channel (1Hz to 1.193182MHz)
 * - Square wave generation mode (mode 3)
 * - Uses standard PIT base frequency of 1.193182 MHz
 * - 16-bit counter per channel
 * - Interrupt generation on channel 0 (IRQ0)
 */

#include "pit.h"
#include "../kernel/ports.h"

/**
 * Initialize a PIT channel with the specified frequency
 * 
 * @param channel: PIT channel number (0-2)
 * @param frequency: Desired frequency in Hz (1-1193182)
 * 
 * Configures the specified PIT channel to generate square waves
 * at the requested frequency. Channel 0 is typically used as
 * the system timer, channel 1 is unused, and channel 2 can
 * control the PC speaker.
 */
void pit_init(uint32_t channel, uint32_t frequency) {
    pit_set_frequency(channel, frequency);
}

/**
 * Set the operating frequency for a PIT channel
 * 
 * @param channel: PIT channel number (0-2)
 * @param frequency: Desired frequency in Hz (1-1193182)
 *
 * Programs the specified PIT channel to generate square waves at
 * the requested frequency. The PIT uses a base frequency of 
 * 1.193182 MHz which is divided to achieve the target frequency.
 * Each channel can be independently configured.
 *
 * The actual frequency achieved may differ slightly from the
 * requested frequency due to integer division of the base
 * frequency.
 */
void pit_set_frequency(uint32_t channel, uint32_t frequency) {
    if (channel > 2 || frequency == 0) return;  // Invalid channel or bad freq
    
    // Calculate divisor from base PIT frequency
    uint32_t divisor = 1193182 / frequency;
    
    // Set up command byte for square wave mode
    uint8_t command = 0;
    switch(channel) {
        case 0:
            command = PIT_CHANNEL0_SELECT | PIT_LOBYTE_HIBYTE | PIT_SQUARE_WAVE;
            break;
        case 1:
            command = PIT_CHANNEL1_SELECT | PIT_LOBYTE_HIBYTE | PIT_SQUARE_WAVE;
            break;
        case 2:
            command = PIT_CHANNEL2_SELECT | PIT_LOBYTE_HIBYTE | PIT_SQUARE_WAVE;
            break;
    }
    
    // Program the PIT
    outb(PIT_COMMAND, command);
    outb((uint16_t)(PIT_CHANNEL0 + channel), (uint8_t)(divisor & 0xFF));         // Low byte
    outb((uint16_t)(PIT_CHANNEL0 + channel), (uint8_t)((divisor >> 8) & 0xFF));  // High byte
} 
