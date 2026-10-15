/* SysTick-driven 10 ms tick source for scout-v2 (UV-K5 V3 / PY32F071).
 *
 * Ported from V1 (uv-k5-v1-dp32g030/Src/scheduler.c). V1 ran on a
 * Cortex-M0 (DP32G030) with a hand-rolled SysTick setup
 * (Src/peripherals/systick.c: SysTick_Config(480000) at a fixed 48 MHz)
 * and a non-standard interrupt entry point (`SystickHandler`, wired
 * through that chip's own vector table). PY32F071 is Cortex-M0+ with a
 * CMSIS-standard vector table (startup_py32f071.s) that expects the
 * handler to be named `SysTick_Handler`; our strong definition here
 * overrides the WEAK stub in startup_py32f071.s. SysTick itself is
 * part of the ARMv6-M core (not vendor silicon), so the timer mechanics
 * are unchanged from V1 — only the reload value and handler name differ.
 *
 * Assumption A4.2: SysTick->LOAD is derived from SystemCoreClock, which
 * defaults to HSI_VALUE = 8 MHz at reset (no clock tree reconfiguration
 * has happened yet in this port — see Src/peripherals/delay.c). The
 * resulting 10 ms period is only as accurate as the HSI oscillator; if
 * a later phase switches to HSE/PLL, SystemCoreClock (and therefore
 * this reload) updates automatically since SysTick_Config reads it at
 * init time. If the on-die HSI trims noticeably off 8 MHz, this tick
 * period will drift proportionally.
 *
 * Application code reads scout_tick_10ms() to schedule its own work
 * (see V1 Src/scout/app.c for the intended usage pattern:
 * `extern uint32_t scout_tick_10ms(void);`).
 */

#include <stdint.h>

#include "py32f0xx.h"
#include "scheduler.h"
#include "peripherals/scheduler.h"

static volatile uint32_t s_tick_10ms;

void SCHEDULER_Init(void)
{
    /* 10 ms period: SystemCoreClock ticks/second / 100 = ticks/10ms.
     * SysTick_Config() takes the reload value directly (it subtracts 1
     * internally) and also enables SysTick's interrupt + counter. */
    (void)SysTick_Config(SystemCoreClock / 100u);
}

void SysTick_Handler(void)
{
    s_tick_10ms++;
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
