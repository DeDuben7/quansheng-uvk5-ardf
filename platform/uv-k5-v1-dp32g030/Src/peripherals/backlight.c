/* backlight implementation — no global state escapes this file. */

#include <stddef.h>

#include "peripherals/backlight.h"
#include "peripherals/backlight_hal.h"

#include "bsp/dp32g030/gpio.h"
#include "bsp/dp32g030/pwmplus.h"
#include "bsp/dp32g030/portcon.h"
#include "peripherals/gpio.h"

static backlight_config_t s_cfg;
static uint16_t           s_countdown_500ms;
static uint8_t            s_current_brightness;
static bool               s_on;
static bool               s_initialized;

static void backlight_init_hardware(void)
{
    // 48 MHz / 1024 / 1000 Hz target -> CLKSRC divider in the upper word.
    const uint32_t PWM_FREQUENCY_HZ = 1000;
    PWM_PLUS0_CLKSRC |= ((48000000 / 1024 / PWM_FREQUENCY_HZ) << 16);
    PWM_PLUS0_PERIOD  = 1023;

    PORTCON_PORTB_SEL0 &= ~PORTCON_PORTB_SEL0_B6_MASK;
    PORTCON_PORTB_SEL0 |=  PORTCON_PORTB_SEL0_B6_BITS_PWMP0_CH0;

    PWM_PLUS0_GEN = PWMPLUS_GEN_CH0_OE_BITS_ENABLE
                  | PWMPLUS_GEN_CH0_OUTINV_BITS_ENABLE;

    PWM_PLUS0_CFG = PWMPLUS_CFG_CNT_REP_BITS_ENABLE
                  | PWMPLUS_CFG_COUNTER_EN_BITS_ENABLE;
}

backlight_status_t backlight_init(const backlight_config_t* cfg)
{
    if (cfg == NULL) return BACKLIGHT_ERROR;
    s_cfg = *cfg;
    backlight_init_hardware();
    backlight_set_brightness(s_cfg.brightness_min);
    s_on = false;
    s_countdown_500ms = 0;
    s_initialized = true;
    return BACKLIGHT_OK;
}

void backlight_turn_on(void)
{
    if (!s_initialized) return;
    s_on = true;
    backlight_set_brightness(s_cfg.brightness_max);
    s_countdown_500ms = s_cfg.timeout_500ms;  // 0 stays 0 = never expires
}

void backlight_turn_off(void)
{
    if (!s_initialized) return;
    s_on = false;
    backlight_set_brightness(s_cfg.brightness_min);
    s_countdown_500ms = 0;
}

bool backlight_is_on(void)
{
    return s_on;
}

void backlight_set_brightness(uint8_t brightness)
{
    s_current_brightness = brightness;
    PWM_PLUS0_CH0_COMP = (1u << brightness) - 1u;
}

uint8_t backlight_get_brightness(void)
{
    return s_current_brightness;
}

bool backlight_task_500ms(void)
{
    if (!s_on || s_countdown_500ms == 0) return false;
    if (--s_countdown_500ms == 0)
    {
        backlight_turn_off();
        return true;
    }
    return false;
}

/* Shared contract for core (peripherals/backlight_hal.h). The auto-off
 * timing lives in core (scout/light.c); timeout_500ms stays 0 here. */
void backlight_hal_set(bool on)
{
    if (on) backlight_turn_on();
    else    backlight_turn_off();
}
