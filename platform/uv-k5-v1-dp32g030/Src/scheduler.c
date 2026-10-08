/* Minimal SysTick-driven scheduler for scout-v2.
 *
 * Increments a free-running 10 ms tick counter and pumps any module that
 * needs periodic work (backlight auto-off, future audio-DF, ...).
 * Application code reads scout_tick_10ms() to schedule its own work.
 */

#include <stdint.h>
#include <stdbool.h>

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
