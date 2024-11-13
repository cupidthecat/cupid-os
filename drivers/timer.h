#ifndef TIMER_H
#define TIMER_H

#include "../kernel/types.h"

void timer_init(uint32_t hz);
uint32_t timer_get_ticks(void);
uint32_t timer_get_frequency(void);
uint32_t timer_get_uptime_ms(void);
void timer_sleep_ms(uint32_t ms);
void timer_delay_us(uint32_t us);

#endif 