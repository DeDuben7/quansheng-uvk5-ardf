/* UV-K5 V3 (PY32F071) battery-voltage ADC front end.
 *
 * PY32's ADC1, channel 8 (PB1) is wired to the battery-voltage divider —
 * see armel-k5v3-src/App/board.c:138-175 (BOARD_ADC_Init /
 * BOARD_ADC_GetBatteryInfo). There is no current-sense channel on V3
 * (armel board.c:174 always sets *pCurrent = 0), so this port only
 * exposes a raw voltage read.
 *
 * Phase 3 Task 2 (A3.6): the battery divider/scaling is carried over
 * unverified from the reference — not re-tuned here.
 */
#ifndef PERIPHERALS_ADC_H
#define PERIPHERALS_ADC_H

#include <stdint.h>

/* Enable ADC1's clock, configure PB1 (channel 8) as an analog input, run
 * the PY32 self-calibration sequence, and arm a single-conversion regular
 * sequence on channel 8. Call once at boot before ADC_GetBatteryRaw(). */
void ADC_Init(void);

/* Run one blocking conversion on ADC1 channel 8 (PB1, battery voltage)
 * and return the 12-bit raw result (0..4095), right-aligned. */
uint16_t ADC_GetBatteryRaw(void);

#endif /* PERIPHERALS_ADC_H */
