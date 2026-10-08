#ifndef CALIB_H
#define CALIB_H

/* Factory calibration block — READ-ONLY.
 *
 * Every UV-K5 hardware version stores the same 512-byte factory calibration
 * layout (V1: EEPROM 0x1E00..0x1FFF, V3: SPI flash 0x010000..0x0101FF).
 * The platform only provides raw access to it (CAL_HW_Read); the layout is
 * decoded here, once, for all platforms.
 *
 *   +0x0D0 + band*16  TX PA bias: LOW, MID, HIGH triplets (3 bytes each:
 *                     band lower edge, middle, upper edge)
 *   +0x140            battery ADC calibration, 6 x uint16; [3] = ADC at 7.60 V
 *   +0x000 / +0x060   squelch tables for UHF / VHF (below 174 MHz): six
 *                     16-byte rows indexed by level (1..9) — open RSSI,
 *                     close RSSI, open noise, close noise, close glitch,
 *                     open glitch (reald RADIO_ConfigureSquelchAndOutputPower)
 */

#include <stdbool.h>
#include <stdint.h>

/* Platform: read len bytes at `offset` within the calibration block. */
void CAL_HW_Read(uint16_t offset, void *buf, uint8_t len);

/* Stock band indices used by the TX table (reald frequencyBandTable). */
#define CALIB_BAND_137 2u   /* 137-174 MHz: covers 2 m */
#define CALIB_BAND_400 5u   /* 400-470 MHz: covers 70 cm */

/* Raw LOW-power triplet for a band; false if blank (0x00/0xFF). */
bool calib_tx_low_triplet(uint8_t band, uint8_t out[3]);

/* LOW-power PA bias interpolated at freq_10hz (BK4819 10 Hz units), as
 * reald FREQUENCY_CalculateOutputPower. false if the frequency is outside
 * the 2 m / 70 cm bands or the calibration is blank. */
bool calib_tx_low_bias(uint32_t freq_10hz, uint8_t *bias);

/* ADC count at 7.60 V, or 0 if blank/implausible. */
uint16_t calib_battery_adc760(void);

/* Factory squelch thresholds, in BK4819_SetupSquelch terms. */
typedef struct {
    uint8_t open_rssi, close_rssi;
    uint8_t open_noise, close_noise;
    uint8_t close_glitch, open_glitch;
} calib_squelch_t;

/* Thresholds for squelch `level` (1..9) at freq_10hz. false if the level is
 * out of range or the table is blank (0xFF / all zero). */
bool calib_squelch(uint32_t freq_10hz, uint8_t level, calib_squelch_t *out);

#endif
