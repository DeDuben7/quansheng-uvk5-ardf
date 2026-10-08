/* Minimal SysTick-driven scheduler for scout-v2 (UV-K5 V3 / PY32F071).
 *
 * See Src/scheduler.c for details.
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

/* Configure and enable the SysTick 10 ms tick. Call once at boot before
 * relying on scout_tick_10ms(). */
void SCHEDULER_Init(void);

uint32_t scout_tick_10ms(void);

#endif /* SCHEDULER_H */
