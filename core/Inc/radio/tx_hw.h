#ifndef RADIO_TX_HW_H
#define RADIO_TX_HW_H

/* Platform contract for transmit (core/Src/radio/radio.c).
 *
 * The TX signal path itself is BK4819-internal and shared; what differs per
 * board is how PTT is wired, where the per-radio PA calibration lives, and
 * the chip variant's microphone-gain range. Each platform implements these in
 * its own Src/radio/tx_hw.c.
 */

#include <stdbool.h>
#include <stdint.h>

/* true while the PTT button is held (raw level, no debounce). */
bool    TX_HW_PttPressed(void);

/* PA bias for REG_36<15:8> at the given frequency (BK4819 10 Hz units).
 * Deliberately a very LOW bench-test power level on every platform. */
uint8_t TX_HW_PaBias(uint32_t freq_10hz);

/* Microphone sensitivity tuning for REG_7D<5:0> (chip-variant specific). */
uint8_t TX_HW_MicGain(void);

#endif
