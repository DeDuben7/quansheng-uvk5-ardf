/* battery: pure module that samples the battery ADC and converts to
 * voltage / percentage. No application globals, no EEPROM access — the
 * caller provides the ADC hook and the calibration value (read from
 * EEPROM 0x1F40..0x1F4B by main, cal word [3] = ADC count at 7.60 V).
 */

#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>

typedef struct {
    // ADC count corresponding to 7.60 V. Factory calibration lives in
    // EEPROM at 0x1F46 (word [3] of the 6-word block at 0x1F40).
    // Pass 0 to fall back to the nominal 1900.
    uint16_t adc_760;
    // Board hook that reads the battery ADC channels (voltage, current).
    void (*read_adc)(uint16_t *voltage, uint16_t *current);
} battery_config_t;

void     battery_init(const battery_config_t *cfg);

// Call once per app tick (~10 ms); samples the ADC every ~500 ms.
void     battery_task(void);

// Averaged battery voltage in 10 mV units (e.g. 792 = 7.92 V).
uint16_t battery_get_voltage_10mV(void);

// Charge estimate 0..100, from the stock 1600 mAh discharge curve.
uint8_t  battery_get_percent(void);

#endif
