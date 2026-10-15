/* Simulated clock. The firmware "spends" time in SYSTEM_DelayMs (the
 * main loop waits 10 ms per pass) and on the BK4819 bus (emu_spend_us, per
 * register access: the DF tone and the scanner read RSSI as fast as the bus
 * allows until the next 10 ms tick), so advancing the clock there — one
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
static uint32_t s_spent_us;   /* bus time not yet a whole millisecond */

uint32_t emu_now_ms(void)
{
    return s_now_ms;
}

uint32_t emu_now_us(void)
{
    return s_now_ms * 1000u + s_spent_us;
}

void emu_spend_us(uint32_t us)
{
    s_spent_us += us;
    while (s_spent_us >= 1000u)
    {
        s_spent_us -= 1000u;
        emu_advance_ms(1);
    }
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

uint32_t scout_time_us(void)
{
    return emu_now_us();
}
