/* audio_df implementation.
 *
 * BK4819 AF=ALAM (0x02, "Tone Out for Rx") routes the Tone1 generator
 * to the speaker while the chip remains in RX-DSP mode. We control the
 * pitch by writing REG_71 with a scaled frequency, and the gain via
 * REG_70's TONE1_TUNING_GAIN field. See LEARNINGS.md in scout-ardf for
 * the full discovery story.
 */

#include <stddef.h>

#include "audio_df/audio_df.h"
#include "bk4819/bk4819.h"
#include "bk4819/bk4819_regs.h"
#include "peripherals/audio_path.h"

// Logarithmic RSSI -> pitch lookup. 18 buckets from 200 Hz to ~2.8 kHz.
static const uint16_t TONE_LUT[18] =
{
    200,  234,  273,  320,  374,  437,  511,  597,  698,
    815,  952, 1112, 1300, 1519, 1775, 2073, 2422, 2829
};
#define TONE_LUT_LEN ((uint8_t)(sizeof(TONE_LUT) / sizeof(TONE_LUT[0])))

static audio_df_config_t s_cfg;
static bool              s_active;
static bool              s_initialized;
static uint16_t          s_last_tone_hz;

static uint16_t rssi_to_tone_hz(uint16_t rssi)
{
    if (rssi <= s_cfg.rssi_floor) return TONE_LUT[0];
    if (rssi >= s_cfg.rssi_ceil)  return TONE_LUT[TONE_LUT_LEN - 1];

    uint32_t span_rssi = s_cfg.rssi_ceil - s_cfg.rssi_floor;
    uint32_t pos       = (uint32_t)(rssi - s_cfg.rssi_floor) * (TONE_LUT_LEN - 1);
    uint8_t  idx       = (uint8_t)(pos / span_rssi);
    if (idx >= TONE_LUT_LEN - 1) return TONE_LUT[TONE_LUT_LEN - 1];

    // Linear interpolate between TONE_LUT[idx] and TONE_LUT[idx+1].
    uint32_t bucket_start = (uint32_t)idx * span_rssi / (TONE_LUT_LEN - 1);
    uint32_t bucket_size  = span_rssi / (TONE_LUT_LEN - 1);
    uint32_t frac         = (uint32_t)(rssi - s_cfg.rssi_floor) - bucket_start;
    uint16_t a = TONE_LUT[idx];
    uint16_t b = TONE_LUT[idx + 1];
    if (bucket_size == 0) return a;
    return (uint16_t)(a + ((uint32_t)(b - a) * frac) / bucket_size);
}

// Write the frequency register. scale_freq() math from the BK4819 driver.
static void set_tone_hz_register(uint16_t hz)
{
    uint32_t scaled = (((uint32_t)hz * 1353245u) + (1u << 16)) >> 17;
    BK4819_WriteRegister(BK4819_REG_71, (uint16_t)scaled);
    s_last_tone_hz = hz;
}

static void enable_tone_path(uint16_t initial_hz)
{
    // Enable Tone1 generator with the configured gain.
    BK4819_WriteRegister(BK4819_REG_70,
        BK4819_REG_70_ENABLE_TONE1
        | ((uint16_t)s_cfg.tone_gain << BK4819_REG_70_SHIFT_TONE1_TUNING_GAIN));
    set_tone_hz_register(initial_hz);
    // Route Tone1 to the speaker, chip stays in RX.
    BK4819_SetAF(BK4819_AF_ALAM);
    AUDIO_AudioPathOn();
}

static void disable_tone_path(void)
{
    BK4819_WriteRegister(BK4819_REG_70, 0);
    BK4819_SetAF(BK4819_AF_MUTE);
}

audio_df_status_t audio_df_init(const audio_df_config_t* cfg)
{
    if (cfg == NULL) return AUDIO_DF_ERROR;
    s_cfg = *cfg;
    s_active = false;
    s_last_tone_hz = 0;
    s_initialized = true;
    return AUDIO_DF_OK;
}

void audio_df_enable(void)
{
    if (!s_initialized || s_active) return;
    s_active = true;
    enable_tone_path(TONE_LUT[TONE_LUT_LEN / 2]);  // start at mid pitch
}

void audio_df_disable(void)
{
    if (!s_initialized || !s_active) return;
    s_active = false;
    disable_tone_path();
}

void audio_df_toggle(void)
{
    if (s_active) audio_df_disable();
    else          audio_df_enable();
}

bool audio_df_is_active(void)
{
    return s_active;
}

void audio_df_set_rssi(uint16_t rssi)
{
    if (!s_active) return;
    uint16_t hz = rssi_to_tone_hz(rssi);
    if (hz != s_last_tone_hz)
    {
        set_tone_hz_register(hz);
    }
}

void audio_df_task(void)
{
    if (!s_active) return;
    // Re-assert AF=ALAM each tick — anything that re-runs the RX state
    // machine would otherwise pull AF back to FM/AM and silence the tone.
    // Every 10 ms, not on every RSSI reading: the bus stays free for RSSI.
    BK4819_SetAF(BK4819_AF_ALAM);
}
