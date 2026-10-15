/* Minimal SysTick-driven scheduler for scout-v2.
 *
 * Increments a free-running 10 ms tick counter and pumps any module that
 * needs periodic work (backlight auto-off, future audio-DF, ...).
 * Application code reads scout_tick_10ms() to schedule its own work.
 */

#include <stdint.h>
#include <stdbool.h>

#include "ARMCM0.h"
#include "peripherals/backlight.h"
#include "peripherals/scheduler.h"

static volatile uint32_t s_tick_10ms;

void SystickHandler(void) __attribute__((interrupt));
void SystickHandler(void)
{
    s_tick_10ms++;

    // 500 ms tick derived from the 10 ms tick.
    if ((s_tick_10ms % 50) == 0)
    {
        backlight_task_500ms();
    }
}

uint32_t scout_tick_10ms(void)
{
    return s_tick_10ms;
}

uint32_t scout_time_us(void)
{
    // Re-read when the tick interrupt ran in between: VAL then belongs to
    // the new tick.
    uint32_t tick, val;
    do
    {
        tick = s_tick_10ms;
        val  = SysTick->VAL;
    } while (tick != s_tick_10ms);
    const uint32_t reload = SysTick->LOAD + 1u;   // SysTick counts down
    return tick * 10000u + (reload - 1u - val) / (reload / 10000u);
}
