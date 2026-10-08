/* V1 (DP32G030) transmit HAL — see core/Inc/radio/tx_hw.h. */

#include "radio/tx_hw.h"
#include "bsp/dp32g030/gpio.h"
#include "peripherals/gpio.h"
#include "calib/calib.h"

/* PTT: GPIOC pin 5, active low, input + pull-up set up in BOARD_Init. */
bool TX_HW_PttPressed(void)
{
	return !GPIO_CheckBit(&GPIOC->DATA, GPIOC_PIN_PTT);
}

/* PA bias: the factory LOW-power calibration (core/calib), scaled by 4/25 —
 * f4hwn's lowest level ("LOW1"), far below stock LOW. On this radio stock LOW
 * is 95 (2 m) / 90 (70 cm), giving ~15 / ~14. */
#define FALLBACK_BIAS 12u

uint8_t TX_HW_PaBias(uint32_t freq_10hz)
{
	uint8_t low;
	if (!calib_tx_low_bias(freq_10hz, &low))
		return FALLBACK_BIAS;
	return (uint8_t)((low * 4u) / 25u);
}

/* BK4819 REG_7D<4:0>: stock default mic level 4 -> 31 (gMicGain_dB2). */
uint8_t TX_HW_MicGain(void)
{
	return 31;
}
