/* Busy-wait delays for UV-K5 V3 (PY32F071).
 *
 * See Src/peripherals/delay.c for the timing-accuracy caveat
 * (assumption A1.5).
 */
#ifndef PERIPHERALS_DELAY_H
#define PERIPHERALS_DELAY_H

#include <stdint.h>

void DelayMs(uint32_t ms);
void DelayUs(uint32_t us);

#endif /* PERIPHERALS_DELAY_H */
