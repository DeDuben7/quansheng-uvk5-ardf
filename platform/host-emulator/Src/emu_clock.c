/* Simulated clock. The firmware only "spends" time in SYSTEM_DelayMs (the
 * main loop waits 10 ms per pass), so advancing the clock there — one
 * millisecond at a time, running due script actions — gives a faithful
 * timeline: renders every 100 ms, long presses after 500 ms, etc. */

#include <stdio.h>
#include <stdlib.h>

#include "emu/emu.h"
#include "peripherals/scheduler.h"
#include "peripherals/system.h"

/* A scene that never finishes is a bug in the scene or the emulator. */
#define EMU_TIME_LIMIT_MS 600000u

static uint32_t s_now_ms;

uint32_t emu_now_ms(void)
{
    return s_now_ms;
}

void emu_advance_ms(uint32_t ms)
{
    while (ms--)
    {
        s_now_ms++;
        emu_bk_tick();
        if (s_now_ms > EMU_TIME_LIMIT_MS)
        {
            fprintf(stderr, "FAIL: scene still running after %u ms\n", EMU_TIME_LIMIT_MS);
            exit(1);
        }
        emu_script_pump();
    }
}

void SYSTEM_DelayMs(uint32_t ms)
{
    emu_advance_ms(ms);
}

uint32_t scout_tick_10ms(void)
{
    return s_now_ms / 10u;
}
