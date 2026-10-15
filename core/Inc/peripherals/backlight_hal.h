/* Shared platform HAL contract: LCD backlight on/off.
 *
 * Each platform keeps its own backlight driver with its own API (V1: PWM
 * brightness, V3: a GPIO; see their Inc/peripherals/backlight.h). This is
 * the one call core code uses; scout/light.c decides when.
 */
#ifndef PERIPHERALS_BACKLIGHT_HAL_H
#define PERIPHERALS_BACKLIGHT_HAL_H

#include <stdbool.h>

void backlight_hal_set(bool on);

#endif /* PERIPHERALS_BACKLIGHT_HAL_H */
