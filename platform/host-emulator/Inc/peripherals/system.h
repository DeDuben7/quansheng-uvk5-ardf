/* Host emulator: busy-wait delay HAL. Advances the simulated clock. */
#ifndef DRIVER_SYSTEM_H
#define DRIVER_SYSTEM_H

#include <stdint.h>

void SYSTEM_DelayMs(uint32_t ms);

#endif
