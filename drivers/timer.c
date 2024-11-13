#include "../kernel/ports.h"
#include "../kernel/irq.h"
#include "../kernel/types.h"

#define PIT_CHANNEL0_DATA 0x40
#define PIT_COMMAND      0x43
#define PIT_FREQUENCY    1193180
#define PIT_MODE3       0x36

static volatile uint32_t tick_count = 0;
static uint32_t frequency = 0;

// Timer interrupt handler
static void timer_callback(struct registers* regs) {
    tick_count++;
}

void timer_init(uint32_t hz) {
    frequency = hz;
    
    // Calculate divisor
    uint32_t divisor = PIT_FREQUENCY / hz;
    
    // Send command byte
    outb(PIT_COMMAND, PIT_MODE3);
    
    // Send divisor
    uint8_t low = (uint8_t)(divisor & 0xFF);
    uint8_t high = (uint8_t)((divisor >> 8) & 0xFF);
    outb(PIT_CHANNEL0_DATA, low);
    outb(PIT_CHANNEL0_DATA, high);
    
    // Install timer handler
    irq_install_handler(0, timer_callback);
}

uint32_t timer_get_ticks(void) {
    return tick_count;
}

uint32_t timer_get_frequency(void) {
    return frequency;
}

uint32_t timer_get_uptime_ms(void) {
    return (tick_count * 1000) / frequency;
}

void timer_sleep_ms(uint32_t ms) {
    uint32_t start = timer_get_ticks();
    uint32_t ticks_to_wait = (ms * frequency) / 1000;
    
    while (timer_get_ticks() - start < ticks_to_wait) {
        __asm__ volatile("hlt");  // Wait for next interrupt
    }
}

void timer_delay_us(uint32_t us) {
    uint32_t start = timer_get_ticks();
    uint32_t ticks_to_wait = (us * frequency) / 1000000;
    
    if (ticks_to_wait == 0) ticks_to_wait = 1;
    
    while (timer_get_ticks() - start < ticks_to_wait) {
        __asm__ volatile("hlt");
    }
} 