/* Shared platform HAL contract: monotonic 10 ms tick counter.
 *
 * Each platform provides its own scheduler implementation (SysTick-driven
 * on both UV-K5 V1 and V3), but core code only relies on this single
 * declaration. The counter is a free-running uint32_t that rolls over;
 * consumers must compare deltas (e.g. `(uint32_t)(scout_tick_10ms() -
 * last)`), never absolute values, so rollover is safe.
 */
#ifndef PERIPHERALS_SCHEDULER_H
#define PERIPHERALS_SCHEDULER_H

#include <stdint.h>

uint32_t scout_tick_10ms(void);

#endif /* PERIPHERALS_SCHEDULER_H */
