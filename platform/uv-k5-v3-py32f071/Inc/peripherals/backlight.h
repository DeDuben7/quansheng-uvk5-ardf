/* LCD backlight on/off for UV-K5 V3 (PY32F071), driven from GPIOF pin 8.
 *
 * This is a new, minimal module for the V3 port (not a port of V1's
 * Src/peripherals/backlight.c, which carries a fuller PWM-brightness +
 * auto-off-timer API tied to V1's scheduler). See Src/peripherals/backlight.c
 * for the pin assumption.
 */
#ifndef PERIPHERALS_BACKLIGHT_H
#define PERIPHERALS_BACKLIGHT_H

void BACKLIGHT_On(void);
void BACKLIGHT_Off(void);

#endif /* PERIPHERALS_BACKLIGHT_H */
