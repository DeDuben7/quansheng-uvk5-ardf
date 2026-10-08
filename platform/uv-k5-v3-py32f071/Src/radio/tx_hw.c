/* V3 (PY32F071) transmit HAL — see core/Inc/radio/tx_hw.h. */

#include "radio/tx_hw.h"
#include "py32f0xx.h"
#include "calib/calib.h"

/* PTT: PB10, active low (armel App/driver/gpio.h GPIO_PIN_PTT). Configured
 * lazily as input + pull-up on first use, like the keyboard matrix. */
bool TX_HW_PttPressed(void)
{
	static bool inited;
	if (!inited)
	{
		RCC->IOPENR |= RCC_IOPENR_GPIOBEN;
		(void)RCC->IOPENR;
		GPIOB->MODER &= ~(3u << (10u * 2u));   /* input */
		GPIOB->PUPDR &= ~(3u << (10u * 2u));
		GPIOB->PUPDR |=  (1u << (10u * 2u));   /* pull-up */
		inited = true;
	}
	return (GPIOB->IDR & (1u << 10)) == 0;
}

/* PA bias: the factory LOW-power calibration from the SPI flash (core/calib),
 * scaled to armel's lowest V3 level: LOW * 4/20 on VHF, * 4/25 on UHF
 * (App/radio.c dividers_band2 / dividers_band5). If the calibration is blank,
 * fall back to those levels applied to a typical LOW of 75 (15 / 12). Very
 * low power — bench testing between two radios only. */
uint8_t TX_HW_PaBias(uint32_t freq_10hz)
{
    const bool vhf = freq_10hz < 28000000u;
    uint8_t low;
    if (!calib_tx_low_bias(freq_10hz, &low))
        return vhf ? 15u : 12u;
    return (uint8_t)((low * 4u) / (vhf ? 20u : 25u));
}

/* BK4829 REG_7D<5:0> has a 0..63 range; armel's default mic level 4 -> 32. */
uint8_t TX_HW_MicGain(void)
{
	return 32;
}
