/*
 * PC Speaker Driver Implementation
 * 
 * This file implements basic PC speaker functionality:
 * - Initializes PIT channel 2 for speaker control
 * - Provides functions to play tones at specific frequencies
 * - Handles speaker enable/disable
 * - Implements basic beep functionality
 */

#include "speaker.h"
#include "../kernel/ports.h"
#include "timer.h"
#include "pit.h"

// PC speaker ports and bits
#define PC_SPEAKER_PORT 0x61
#define PC_SPEAKER_ENABLE_BITS 0x03
#define PC_SPEAKER_GATE_BIT 0x01
#define PC_SPEAKER_DATA_BIT 0x02

void pc_speaker_on(uint32_t frequency) {
    // Calculate PIT divisor for the desired frequency
    uint32_t divisor = 1193180 / frequency;
    
    // Configure PIT channel 2 for square wave generation
    pit_set_frequency(2, frequency);
    
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
    pc_speaker_on(1000);  // 1kHz frequency
    
    // Use a more reliable delay
    timer_sleep_ms(100);  // Sleep for 100 ms
    
    pc_speaker_off();
    
    // Additional delay between beeps
    timer_sleep_ms(50);   // Sleep for 50 ms
} 

     
     
/**
 * test_speaker - Test PC speaker functionality
 * 
 * Plays a series of tones to demonstrate PC speaker control:
 * 1. Simple beep
 * 2. Rising tone
 * 3. Falling tone
 */
/**
 * test_speaker - Test PC speaker functionality
 * 
 * Plays a series of tones to demonstrate PC speaker control:
 * 1. Simple beep
 * 2. Rising tone
 * 3. Falling tone

 * Placing this test anywhere will cause the kernel to not be able to use the keyboard driver
void test_speaker(void) {
    print("Testing PC Speaker...\n");
    
    // Simple beep
    print("Simple beep...\n");
    beep();
    
    // Add a delay between tests
    for(volatile int i = 0; i < 1000000; i++) {
        __asm__ volatile("nop");
    }
    
    // Rising tone
    print("Rising tone...\n");
    for(uint32_t freq = 100; freq < 2000; freq += 50) {
        pc_speaker_on(freq);
        // More reliable delay
        for(volatile int i = 0; i < 100000; i++) {
            __asm__ volatile("nop");
        }
    }
    pc_speaker_off();
    
    // Add a delay between tests
    for(volatile int i = 0; i < 1000000; i++) {
        __asm__ volatile("nop");
    }
    
    // Falling tone
    print("Falling tone...\n");
    for(uint32_t freq = 2000; freq > 100; freq -= 50) {
        pc_speaker_on(freq);
        // More reliable delay
        for(volatile int i = 0; i < 100000; i++) {
            __asm__ volatile("nop");
        }
    }
    pc_speaker_off();
    
    print("Speaker test complete.\n");
}
 */
