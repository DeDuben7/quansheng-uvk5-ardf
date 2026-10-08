/* V3 (PY32F071) busy-wait delays.
 *
 * Counts real SysTick cycles, like V1's SYSTICK_DelayUs. SysTick is already
 * running as the 10 ms scheduler tick (reload = SystemCoreClock / 100); we
 * only read its down-counting VAL register, so the tick interrupt is
 * unaffected.
 *
 * The previous version was a volatile nop loop that assumed 4 CPU cycles per
 * iteration. Real iterations take ~4-5x longer, so every delay ran that much
 * too long (measured: 2.1 s from main() to the welcome screen instead of
 * ~0.45 s, and a ~7 s welcome hold instead of 1.5 s).
 */
#include "peripherals/delay.h"
#include "py32f0xx.h"

void DelayUs(uint32_t us)
{
    if ((SysTick->CTRL & SysTick_CTRL_ENABLE_Msk) == 0u)
    {
        /* Only before SCHEDULER_Init(): coarse fallback, errs long. */
        for (volatile uint32_t i = 0; i < (SystemCoreClock / 4000000u) * us + 1u; i++)
        {
            __asm volatile ("nop");
        }
        return;
    }

    const uint32_t reload = SysTick->LOAD + 1u;
    const uint32_t ticks  = us * (SystemCoreClock / 1000000u);
    uint32_t prev    = SysTick->VAL;
    uint32_t elapsed = 0;

    while (elapsed < ticks)
    {
        const uint32_t now = SysTick->VAL;
        /* Down-counter: wraps from 0 back to LOAD. */
        elapsed += (now <= prev) ? (prev - now) : (prev + reload - now);
        prev = now;
    }
}

void DelayMs(uint32_t ms)
{
    while (ms-- > 0u)
    {
        DelayUs(1000u);
    }
}
