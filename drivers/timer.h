#ifndef TIMER_H
#define TIMER_H

#include "../kernel/types.h"
#include "../kernel/isr.h"
#include "../kernel/kernel.h"

// Initialize system timer
void timer_init(uint32_t hz);

// Timer information
uint64_t timer_get_ticks(void);
uint32_t timer_get_frequency(void);
uint32_t timer_get_uptime_ms(void);

// Sleep and delay functions
void timer_sleep_ms(uint32_t ms);
void timer_delay_us(uint32_t us);

// Measurement functions
void timer_start_measure(timer_measure_t* measure);
uint64_t timer_end_measure(timer_measure_t* measure);

// Multi-channel support
typedef void (*timer_callback_t)(struct registers*, uint32_t channel);
bool timer_configure_channel(uint8_t channel, uint32_t frequency, timer_callback_t callback);

// Add these function declarations
void timer_irq_handler(struct registers* r);

#endif 