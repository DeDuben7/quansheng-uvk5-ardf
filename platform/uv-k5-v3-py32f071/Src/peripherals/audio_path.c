/* V3 (PY32F071) audio path enable/disable — GPIOA pin 8.
 *
 * V1's Inc/peripherals/audio_path.h implements these two functions inline
 * against DP32 GPIOC pin 4 (GPIOC_PIN_AUDIO_PATH) via GPIO_SetBit/ClearBit
 * on &GPIOC->DATA. Ported here to PY32 GPIOA pin 8, using the same raw
 * CMSIS-register style already established in this port (see
 * Src/peripherals/spi_lcd.c and main.c): enable the GPIOA clock, configure
 * PA8 as a push-pull output, then drive it via BSRR (high = audio path on).
 *
 * Pin choice matches armel's App/driver/gpio.h:
 *   GPIO_PIN_AUDIO_PATH = GPIO_MAKE_PIN(GPIOA, LL_GPIO_PIN_8)
 * and its GPIO_EnableAudioPath()/GPIO_DisableAudioPath() (SetOutputPin /
 * ResetOutputPin) — same polarity: high = enabled.
 */
#include "peripherals/audio_path.h"
#include "py32f0xx.h"

#define AUDIO_PATH_PIN 8u /* PA8 */

static void audio_path_gpio_init(void)
{
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
    (void)RCC->IOPENR; /* read-back: ensure the enable took effect */

    /* MODER = 01 (general-purpose output) for PA8. */
    GPIOA->MODER &= ~(3u << (AUDIO_PATH_PIN * 2u));
    GPIOA->MODER |=  (1u << (AUDIO_PATH_PIN * 2u));

    /* Push-pull output type (reset default 0). */
    GPIOA->OTYPER &= ~(1u << AUDIO_PATH_PIN);
}

void AUDIO_AudioPathOn(void)
{
    audio_path_gpio_init();
    GPIOA->BSRR = (1u << AUDIO_PATH_PIN);
}

void AUDIO_AudioPathOff(void)
{
    audio_path_gpio_init();
    GPIOA->BSRR = (1u << (AUDIO_PATH_PIN + 16u));
}
