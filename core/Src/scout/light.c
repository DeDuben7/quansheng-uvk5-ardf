/* LCD backlight mode — see scout/light.h.
 *
 * Runs in the main loop on the 10 ms tick, not in the SysTick interrupt, so
 * the backlight is only switched from one context.
 */

#include <stdbool.h>
#include <stdint.h>

#include "peripherals/backlight_hal.h"
#include "peripherals/scheduler.h"
#include "scout/light.h"

#define AUTO_OFF_TICKS (LIGHT_AUTO_OFF_MS / 10u)

static light_mode_t s_mode;
static bool         s_lit;
static uint32_t     s_activity_tick;   // last key press or PTT

static void light_apply(bool on)
{
    if (on == s_lit) return;
    s_lit = on;
    backlight_hal_set(on);
}

void light_init(void)
{
    s_mode = LIGHT_AUTO;
    s_activity_tick = scout_tick_10ms();
    s_lit = true;
    backlight_hal_set(true);
}

void light_set_mode(light_mode_t mode)
{
    if (mode >= LIGHT_MODE_COUNT) return;
    s_mode = mode;
    // Choosing automatic is itself a key press: start with the light on.
    s_activity_tick = scout_tick_10ms();
    light_apply(mode != LIGHT_OFF);
}

light_mode_t light_get_mode(void)
{
    return s_mode;
}

void light_activity(void)
{
    s_activity_tick = scout_tick_10ms();
    if (s_mode == LIGHT_AUTO)
        light_apply(true);
}

void light_task(void)
{
    if (s_mode == LIGHT_AUTO && s_lit
        && (scout_tick_10ms() - s_activity_tick) >= AUTO_OFF_TICKS)
        light_apply(false);
}
