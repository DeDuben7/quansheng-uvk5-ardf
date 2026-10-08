/* LCD backlight on/off for UV-K5 V3 (PY32F071).
 *
 * Assumption A4.1: backlight is driven from GPIOF pin 8 (PF8), per the
 * task brief. This has not been cross-checked against a V3 schematic;
 * revisit if the real backlight net turns out to live elsewhere.
 *
 * Follows the same MODER/BSRR register model as
 * Src/peripherals/spi_lcd.c: 2 bits/pin in MODER (01 = general-purpose
 * output), BSRR bit N sets the pin high, bit N+16 drives it low
 * (read-modify-write-free, so no race with the SysTick handler).
 */
#include "peripherals/backlight.h"
#include "py32f0xx.h"

#define BACKLIGHT_PIN 8u

static void backlight_gpio_init(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOFEN;
    (void)RCC->IOPENR; /* read-back: ensure the enable took effect */

    /* PF8 as general-purpose output, push-pull (OTYPER reset default). */
    GPIOF->MODER &= ~(3u << (BACKLIGHT_PIN * 2u));
    GPIOF->MODER |=  (1u << (BACKLIGHT_PIN * 2u));
}

void BACKLIGHT_On(void)
{
    backlight_gpio_init();
    GPIOF->BSRR = (1u << BACKLIGHT_PIN);
}

void BACKLIGHT_Off(void)
{
    backlight_gpio_init();
    GPIOF->BSRR = (1u << (BACKLIGHT_PIN + 16u));
}
