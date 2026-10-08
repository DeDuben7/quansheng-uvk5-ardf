#ifndef DRIVER_SYSTEM_H
#define DRIVER_SYSTEM_H

/* V3 (PY32F071) shim for V1's peripherals/system.h.
 *
 * The MCU-agnostic scout app (Src/scout/app.c) is copied verbatim from V1 and
 * includes "peripherals/system.h" for SYSTEM_DelayMs(). On V3 the equivalent
 * lives in the PY32 HAL as DelayMs() (peripherals/delay.h). This shim maps the
 * one symbol app.c actually uses onto the HAL so app.c stays byte-identical to
 * V1. (V1's SYSTEM_ConfigureClocks() is not needed — SystemInit handles the
 * PY32 clock at reset.)
 */
#include <stdint.h>

#include "peripherals/delay.h"

static inline void SYSTEM_DelayMs(uint32_t ms) { DelayMs(ms); }

#endif
