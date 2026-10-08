/* Factory calibration decoding — see calib/calib.h. */

#include "calib/calib.h"

typedef struct { uint32_t lower, upper; } band_t;   /* 10 Hz units */

static const band_t BAND_137 = { 13700000u, 17400000u };
static const band_t BAND_400 = { 40000000u, 47000000u };

#define TX_TABLE_OFFSET  0x0D0u
#define BATTERY_OFFSET   0x140u
#define SQUELCH_UHF_OFFSET 0x000u
#define SQUELCH_VHF_OFFSET 0x060u   /* reald radio.c: bands below 174 MHz */
#define SQUELCH_VHF_BELOW  17400000u   /* 10 Hz units */

bool calib_tx_low_triplet(uint8_t band, uint8_t out[3])
{
    CAL_HW_Read((uint16_t)(TX_TABLE_OFFSET + band * 16u), out, 3);   /* LOW = first 3 */
    for (unsigned i = 0; i < 3; i++)
        if (out[i] == 0x00u || out[i] == 0xFFu)
            return false;
    return true;
}

static uint8_t interpolate(const uint8_t t[3], const band_t *b, uint32_t f)
{
    const int32_t lo = (int32_t)b->lower, hi = (int32_t)b->upper, mid = (lo + hi) / 2;
    const int32_t fr = (int32_t)f;
    if (fr <= lo) return t[0];
    if (fr >= hi) return t[2];
    if (fr <= mid) return (uint8_t)(t[1] + ((t[1] - t[0]) * (fr - lo)) / (mid - lo));
    return (uint8_t)(t[1] + ((t[2] - t[1]) * (fr - mid)) / (hi - mid));
}

bool calib_tx_low_bias(uint32_t freq_10hz, uint8_t *bias)
{
    const band_t *b;
    uint8_t band;
    if (freq_10hz >= BAND_137.lower && freq_10hz < BAND_137.upper)      { b = &BAND_137; band = CALIB_BAND_137; }
    else if (freq_10hz >= BAND_400.lower && freq_10hz < BAND_400.upper) { b = &BAND_400; band = CALIB_BAND_400; }
    else return false;

    uint8_t t[3];
    if (!calib_tx_low_triplet(band, t))
        return false;
    *bias = interpolate(t, b, freq_10hz);
    return true;
}

uint16_t calib_battery_adc760(void)
{
    uint16_t w[6];
    CAL_HW_Read(BATTERY_OFFSET, w, sizeof(w));
    /* Plausible 7.60 V counts are ~1500..2600 on both ADCs seen so far. */
    return (w[3] >= 1000u && w[3] < 4000u) ? w[3] : 0u;
}

bool calib_squelch(uint32_t freq_10hz, uint8_t level, calib_squelch_t *out)
{
    if (level < 1u || level > 9u)
        return false;
    const uint16_t base = (uint16_t)(((freq_10hz < SQUELCH_VHF_BELOW) ? SQUELCH_VHF_OFFSET
                                                                       : SQUELCH_UHF_OFFSET) + level);
    uint8_t v[6];
    uint8_t any = 0;
    for (unsigned i = 0; i < 6; i++)
    {
        CAL_HW_Read((uint16_t)(base + i * 0x10u), &v[i], 1);
        if (v[i] == 0xFFu)
            return false;   /* blank EEPROM / flash */
        any |= v[i];
    }
    if (any == 0)
        return false;
    out->open_rssi    = v[0];
    out->close_rssi   = v[1];
    out->open_noise   = v[2];
    out->close_noise  = v[3];
    out->close_glitch = v[4];
    out->open_glitch  = v[5];
    return true;
}
