/* battery implementation. See battery.h for the module contract. */

#include <stddef.h>

#include "battery/battery.h"

#define SAMPLE_WINDOW     4     // average this many ADC readings
#define SAMPLE_PERIOD_TK  50    // one sample per 50 ticks (~500 ms at 10 ms)

// Last-resort fallback for the ADC count at 7.60 V, used only if a platform
// passes a nonsensical battery_config_t.adc_760 (0 or >= 5000). This value is
// per-radio: every platform is expected to supply its own via battery_init()
// (V1 reads the factory word from EEPROM 0x1F40; V3 has no storage and passes
// a value measured against a multimeter). Treat a reading derived from this
// fallback as "unknown", not as a calibrated measurement.
#define ADC_760_FALLBACK   1900

static battery_config_t s_cfg;
static uint16_t         s_samples[SAMPLE_WINDOW];
static uint8_t          s_sample_idx;
static uint8_t          s_tick_cnt;
static uint16_t         s_voltage_10mV;
static uint8_t          s_percent;

// Stock 1600 mAh pack discharge curve, {voltage_10mV, percent} pairs,
// descending. From reald's Voltage2PercentageTable (battery.c).
static const uint16_t CURVE[][2] = {
    { 828, 100 },
    { 814,  97 },
    { 760,  25 },
    { 729,   6 },
    { 630,   0 },
};
#define CURVE_LEN (sizeof(CURVE) / sizeof(CURVE[0]))

static uint8_t volts_to_percent(uint16_t v)
{
    if (v >= CURVE[0][0]) return 100;
    for (unsigned i = 1; i < CURVE_LEN; i++)
    {
        if (v > CURVE[i][0])
        {
            // Linear interpolation between curve points i-1 and i.
            const uint16_t v_hi = CURVE[i - 1][0], p_hi = CURVE[i - 1][1];
            const uint16_t v_lo = CURVE[i][0],     p_lo = CURVE[i][1];
            return (uint8_t)(p_lo + (uint32_t)(v - v_lo) * (p_hi - p_lo)
                                    / (v_hi - v_lo));
        }
    }
    return 0;
}

static void sample_and_update(void)
{
    uint16_t voltage = 0, current = 0;
    s_cfg.read_adc(&voltage, &current);

    s_samples[s_sample_idx] = voltage;
    s_sample_idx = (uint8_t)((s_sample_idx + 1) % SAMPLE_WINDOW);

    uint32_t sum = 0;
    for (unsigned i = 0; i < SAMPLE_WINDOW; i++)
        sum += s_samples[i];
    const uint16_t avg = (uint16_t)(sum / SAMPLE_WINDOW);

    // Scale raw ADC to 10 mV units: cal value = reading at 7.60 V.
    s_voltage_10mV = (uint16_t)(((uint32_t)avg * 760u) / s_cfg.adc_760);
    s_percent      = volts_to_percent(s_voltage_10mV);
}

void battery_init(const battery_config_t *cfg)
{
    if (cfg == NULL || cfg->read_adc == NULL) return;
    s_cfg = *cfg;
    if (s_cfg.adc_760 == 0 || s_cfg.adc_760 >= 5000)
        s_cfg.adc_760 = ADC_760_FALLBACK;

    // Prime the whole window with one real reading so the first
    // displayed value isn't an average with zeros.
    uint16_t voltage = 0, current = 0;
    s_cfg.read_adc(&voltage, &current);
    for (unsigned i = 0; i < SAMPLE_WINDOW; i++)
        s_samples[i] = voltage;
    s_sample_idx = 0;
    sample_and_update();
}

void battery_task(void)
{
    if (s_cfg.read_adc == NULL) return;
    if (++s_tick_cnt < SAMPLE_PERIOD_TK) return;
    s_tick_cnt = 0;
    sample_and_update();
}

uint16_t battery_get_voltage_10mV(void)
{
    return s_voltage_10mV;
}

uint8_t battery_get_percent(void)
{
    return s_percent;
}
