/* radio implementation. Owns the receive state, plus a PIN-gated bench-test
 * transmit path (see radio_tx_start). */

#include <stddef.h>

#include "radio/radio.h"
#include "bk4819/bk4819.h"
#include "bk4819/bk4819_regs.h"
#include "calib/calib.h"
#include "peripherals/audio_path.h"
#include "peripherals/system.h"
#include "radio/tx_hw.h"
#include "scout/config.h"

static uint32_t           s_freq_hz;
static radio_modulation_t s_mod;
static radio_bandwidth_t  s_bw;
static uint8_t            s_gain_idx = RADIO_GAIN_DEFAULT;
static bool               s_mistune;
static bool               s_initialized;
static bool               s_tx;

// Squelch. s_sql_active: level 1..9 AND this band's factory table is
// usable (otherwise the squelch is simply open). s_sql_open mirrors the
// chip's squelch state while active (true = signal present, audio passes).
static uint8_t            s_sql_level;
static bool               s_sql_active;
static bool               s_sql_open;
static bool               s_audio_hold;   // audio_df owns the speaker
// RSSI thresholds after reald's adjustment, before gain compensation, so a
// gain change only has to rewrite REG_78.
static int                s_sql_rssi_open;
static int                s_sql_rssi_close;

static void squelch_apply(bool keep_tone);
static void squelch_rssi_update(void);
static void audio_apply(void);

// Mistune: how far to detune the RX when stepping DOWN past minimum gain.
// Off-channel filter rejection then attenuates beyond the front-end's
// -79 dB floor (reald's ARDF_DoMistuneFreq trick for the final approach).
// 0 = disabled, matching reald ARDF-v0.6 stock behavior
// (ARDF_GAIN_MISTUNE_HZ_DEFAULT = 0; his menu configures it in 200 Hz
// steps). Set to e.g. 2400 to enable a fixed detune step here.
#define MISTUNE_SHIFT_HZ 0u

// REG_13 fields:  [9:8] LNA1   [7:5] LNA2   [4:3] MIX   [2:0] PGA
//
// Ported from reald app/ardf.c ardf_gain_table[] ("new measurement
// uv-k5+ 251215"). The register values are deliberately NON-monotonic:
// the per-field dB values in the datasheet do not match real silicon,
// so reald MEASURED the actual attenuation of each value on a real
// radio and ordered the table by measured dB (5 dB per step).
//
// IMPORTANT: these dB values were measured on reald's UV-K5+. Chips
// vary; if steps sound out of order on our radio, this table must be
// re-measured on OUR device (steady carrier + RSSI readout: raw/2 =
// dB, so 5 dB = 10 RSSI counts per step). The earlier LNA2-only sweep
// (0x031F..0x03FF) is a known-monotonic 24 dB fallback ladder.
typedef struct
{
    uint16_t reg_val;
    int8_t   gain_db;   // relative to full gain (0), negative = attenuated
} gain_entry_t;

static const gain_entry_t GAIN_TABLE[RADIO_GAIN_MAX + 1] =
{
    { 0x0000, -79 },   //  0
    { 0x0109, -74 },   //  1
    { 0x0148, -69 },   //  2
    { 0x012A, -64 },   //  3
    { 0x0104, -59 },   //  4
    { 0x000E, -54 },   //  5
    { 0x020D, -49 },   //  6
    { 0x020F, -44 },   //  7
    { 0x0057, -39 },   //  8
    { 0x008D, -34 },   //  9
    { 0x00C6, -29 },   // 10
    { 0x02FB, -24 },   // 11
    { 0x03EB, -19 },   // 12
    { 0x03D4, -14 },   // 13
    { 0x03FC,  -9 },   // 14
    { 0x03FE,  -4 },   // 15
    { 0x03FF,   0 },   // 16
};

// Program the PLL and RX filter path. The BK4819 works in units of
// 10 Hz (reald: 14550000 == 145.5 MHz) — our public API is in Hz, so
// convert HERE and nowhere else. Passing raw Hz programmed the PLL 10x
// too high AND picked the UHF LNA input for 2 m signals.
static void tune(uint32_t hz)
{
    const uint32_t f10 = hz / 10u;
    BK4819_PickRXFilterPathBasedOnFrequency(f10);
    BK4819_SetFrequency(f10);

    // Re-trigger VCO calibration after every retune, as reald does in
    // ARDF_DoMistuneFreq — without it the new frequency may not take.
    const uint16_t reg = BK4819_ReadRegister(BK4819_REG_30);
    BK4819_WriteRegister(BK4819_REG_30, reg & ~BK4819_REG_30_ENABLE_VCO_CALIB);
    BK4819_WriteRegister(BK4819_REG_30, reg);
}

static void apply_gain(void)
{
    // Keep the AGC pinned in fixed mode at index 3 (= REG_13); the call
    // is a cheap no-op when already fixed.
    BK4819_SetAGC(false);
    BK4819_WriteRegister(BK4819_REG_13, GAIN_TABLE[s_gain_idx].reg_val);
    // The squelch RSSI thresholds depend on the gain (gain compensation).
    if (s_initialized)
        squelch_rssi_update();
}

static BK4819_AF_Type_t af_for_modulation(radio_modulation_t m)
{
    return (m == RADIO_MOD_AM) ? BK4819_AF_AM : BK4819_AF_FM;
}

static BK4819_FilterBandwidth_t bw_to_bk(radio_bandwidth_t bw)
{
    switch (bw)
    {
        case RADIO_BW_WIDE:       return BK4819_FILTER_BW_WIDE;
        case RADIO_BW_NARROW:     return BK4819_FILTER_BW_NARROW;
        case RADIO_BW_NARROW_1K7: return BK4819_FILTER_BW_U1K7;
        default:                  return BK4819_FILTER_BW_WIDE;
    }
}

static void rx_setup(void);

radio_status_t radio_init(const radio_config_t* cfg)
{
    if (cfg == NULL) return RADIO_ERROR;
    s_freq_hz = cfg->initial_freq_hz;
    s_mod     = cfg->initial_modulation;
    s_bw      = cfg->initial_bandwidth;
    s_sql_level = (cfg->initial_squelch > RADIO_SQL_MAX) ? RADIO_SQL_MAX : cfg->initial_squelch;

    rx_setup();

    s_initialized = true;
    return RADIO_OK;
}

// Full receive configuration. Run at boot and again after every
// transmission, so RX always comes back in exactly the boot state.
static void rx_setup(void)
{
    BK4819_RX_TurnOn();
    // Route the antenna to the receiver: GPIO0/pin28 drives the external
    // RF front-end switch. reald sets this in RADIO_SetupRegisters
    // (radio.c:603); without it the chip only hears its own internal
    // noise (~46 raw) no matter what the antenna picks up.
    BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, true);
    tune(s_freq_hz);
    BK4819_SetFilterBandwidth(bw_to_bk(s_bw), false);
    // Match reald's ARDF gain path exactly: do NOT call BK4819_InitAGC
    // here. Calling it would overwrite REG_10..REG_14 with reald's
    // default AGC-table values, and on this chip revision that breaks
    // the predictable REG_13-only control we want. Instead, just put
    // the AGC into fixed mode (fix-index=3 = REG_13) and write our
    // gain register directly.
    apply_gain();

    // Make sure nothing else in the chip is muting/blocking the AF path.
    BK4819_DisableScramble();
    BK4819_DisableVox();
    BK4819_DisableDTMF();
    BK4819_SetupPowerAmplifier(0, 0);

    // AF RX gain + DAC gain. REG_48 bits:
    //   15:12 (constant)      = 11
    //   11:10 AF Rx Gain-1    = 0
    //    9:4  AF Rx Gain-2    = 58 (max-ish, plenty loud)
    //    3:0  AF DAC Gain     = 8
    // Without this, the chip boots with the AF path nearly silent.
    BK4819_WriteRegister(BK4819_REG_48, (11u << 12) | (0u << 10) | (58u << 4) | (8u << 0));

    // Squelch thresholds + interrupt mask for the current level (level 0 =
    // fully open: hear the noise floor so the user can read pitch changes
    // when sweeping the antenna). Zeroes REG_70 like it always did here.
    squelch_apply(false);

    // "TX mute" also gates the AF DAC during RX — must be lifted or no
    // audio comes through, regardless of how loud the chip is producing.
    BK4819_ExitTxMute();
    // AF + speaker amp on, or muted while the squelch is closed.
    audio_apply();
}

// --- Squelch -----------------------------------------------------------------

static int clamp_int(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

// Throw away pending chip interrupts (bounded; reald loops unbounded).
static void irq_drain(void)
{
    for (uint8_t i = 0; i < 8 && (BK4819_ReadRegister(BK4819_REG_0C) & 1u); i++)
        BK4819_WriteRegister(BK4819_REG_02, 0);
}

// Compensated RSSI thresholds -> REG_78 (open << 8 | close), the only
// squelch register that depends on the front-end gain.
static uint16_t squelch_reg78(void)
{
    int open = s_sql_rssi_open, close = s_sql_rssi_close;
#if SCOUT_SQL_GAIN_COMPENSATION
    // The chip compares the RSSI after our fixed front-end gain; the
    // factory thresholds assume full gain. 0.5 dB per RSSI count.
    const int comp = -2 * GAIN_TABLE[s_gain_idx].gain_db;
    open  -= comp;
    close -= comp;
#endif
    return (uint16_t)((clamp_int(open, 0, 255) << 8) | clamp_int(close, 0, 255));
}

// Fully reprogram the squelch for s_sql_level at s_freq_hz.
// BK4819_SetupSquelch mutes the AF and restarts RX, so the chip's squelch
// starts closed again: s_sql_open is reset to match (a carrier on the air
// re-opens it with SQUELCH_LOST). Callers must audio_apply() afterwards.
// SetupSquelch also zeroes REG_70 (the tone generator); keep_tone restores
// it so the audio_df tone survives.
static void squelch_apply(bool keep_tone)
{
    const uint16_t reg70 = BK4819_ReadRegister(BK4819_REG_70);
    calib_squelch_t t;

    s_sql_active = s_sql_level != 0 && calib_squelch(s_freq_hz / 10u, s_sql_level, &t);
    if (!s_sql_active)
    {
        // Fully open: level 0, or no usable factory table for this band.
        BK4819_SetupSquelch(0, 0, 0, 0, 255, 255);
        BK4819_WriteRegister(BK4819_REG_3F, 0);
        irq_drain();
        s_sql_open = true;
    }
    else
    {
        // reald ENABLE_SQUELCH_MORE_SENSITIVE (its default), verbatim:
        // a more sensitive open threshold, then keep close != open.
        int rssi_open    = t.open_rssi / 2;
        int rssi_close   = t.close_rssi;
        int noise_open   = t.open_noise * 2;
        int noise_close  = t.close_noise;
        int glitch_open  = t.open_glitch * 2;
        int glitch_close = t.close_glitch;
        if (rssi_close == rssi_open && rssi_close >= 2)        rssi_close -= 2;
        if (noise_close == noise_open && noise_close <= 125)   noise_close += 2;
        if (glitch_close == glitch_open && glitch_close <= 253) glitch_close += 2;
        s_sql_rssi_open  = rssi_open;
        s_sql_rssi_close = rssi_close;

        const uint16_t reg78 = squelch_reg78();
        BK4819_SetupSquelch(
            (uint8_t)(reg78 >> 8),                     (uint8_t)reg78,
            (uint8_t)clamp_int(noise_open,   0, 127), (uint8_t)clamp_int(noise_close, 0, 127),
            (uint8_t)clamp_int(glitch_close, 0, 255), (uint8_t)clamp_int(glitch_open, 0, 255));
        irq_drain();
        BK4819_WriteRegister(BK4819_REG_3F,
                             BK4819_REG_3F_SQUELCH_FOUND | BK4819_REG_3F_SQUELCH_LOST);
        s_sql_open = false;
    }

    if (keep_tone)
        BK4819_WriteRegister(BK4819_REG_70, reg70);
}

// Gain changed: only the RSSI thresholds move. No RX restart, so the
// squelch state, the AF output and the DF tone are untouched.
static void squelch_rssi_update(void)
{
    if (s_sql_active)
        BK4819_WriteRegister(BK4819_REG_78, squelch_reg78());
}

// Route the receive audio: on while the squelch is open (or off), muted
// while it is closed. Leaves the speaker alone during TX and while
// audio_df holds it.
static void audio_apply(void)
{
    if (s_tx || s_audio_hold)
        return;
    if (!s_sql_active || s_sql_open)
    {
        BK4819_SetAF(af_for_modulation(s_mod));
        BK4819_ExitTxMute();
        // Speaker amp GPIO must be high to route audio to the speaker/jack.
        AUDIO_AudioPathOn();
    }
    else
    {
        BK4819_SetAF(BK4819_AF_MUTE);
        AUDIO_AudioPathOff();
    }
}

void radio_set_squelch(uint8_t level)
{
    if (level > RADIO_SQL_MAX) level = RADIO_SQL_MAX;
    if (level == s_sql_level) return;
    s_sql_level = level;
    squelch_apply(true);
    audio_apply();
}

uint8_t radio_get_squelch(void)
{
    return s_sql_level;
}

void radio_squelch_task(void)
{
    if (!s_initialized || s_tx || !s_sql_active)
        return;

    bool changed = false;
    for (uint8_t i = 0; i < 8 && (BK4819_ReadRegister(BK4819_REG_0C) & 1u); i++)
    {
        BK4819_WriteRegister(BK4819_REG_02, 0);
        const uint16_t flags = BK4819_ReadRegister(BK4819_REG_02);
        // reald naming (app.c CheckRadioInterrupts): SQUELCH_LOST means the
        // squelch OPENED (signal present); SQUELCH_FOUND means it closed.
        if (flags & BK4819_REG_02_SQUELCH_LOST)  { s_sql_open = true;  changed = true; }
        if (flags & BK4819_REG_02_SQUELCH_FOUND) { s_sql_open = false; changed = true; }
    }
    if (changed)
        audio_apply();
}

void radio_squelch_resync(void)
{
    squelch_apply(true);
    audio_apply();
}

bool radio_squelch_is_open(void)
{
    return !s_sql_active || s_sql_open;
}

void radio_audio_hold(bool hold)
{
    s_audio_hold = hold;
    if (!hold)
        audio_apply();
}

void radio_set_frequency(uint32_t hz)
{
    // A manual retune is a fresh start: drop any active mistune so the
    // radio is actually ON the frequency the user just entered.
    s_mistune = false;
    s_freq_hz = hz;
    tune(hz);
    // VHF and UHF have their own squelch tables; a new channel also starts
    // with the squelch closed until the chip finds a signal.
    if (s_sql_level != 0)
    {
        squelch_apply(true);
        audio_apply();
    }
}

uint32_t radio_get_frequency(void)
{
    return s_freq_hz;
}

void radio_tune_raw(uint32_t hz)
{
    const uint32_t f10 = hz / 10u;   // BK4819 unit = 10 Hz
    BK4819_SetFrequency(f10);

    const uint16_t reg = BK4819_ReadRegister(BK4819_REG_30);
    BK4819_WriteRegister(BK4819_REG_30, reg & ~BK4819_REG_30_ENABLE_VCO_CALIB);
    BK4819_WriteRegister(BK4819_REG_30, reg);
}

void radio_set_modulation(radio_modulation_t mod)
{
    s_mod = mod;
    // Same rule as init: keep AGC fixed at REG_13 + our table. No InitAGC.
    apply_gain();
    // New AF mode, unless the squelch is closed or audio_df holds the speaker.
    audio_apply();
}

radio_modulation_t radio_get_modulation(void)
{
    return s_mod;
}

void radio_set_bandwidth(radio_bandwidth_t bw)
{
    s_bw = bw;
    BK4819_SetFilterBandwidth(bw_to_bk(bw), false);
}

radio_bandwidth_t radio_get_bandwidth(void)
{
    return s_bw;
}

void radio_set_gain_index(uint8_t idx)
{
    if (idx > RADIO_GAIN_MAX) idx = RADIO_GAIN_MAX;
    s_gain_idx = idx;
    apply_gain();
}

void radio_set_raw_gain(uint16_t reg13)
{
    BK4819_SetAGC(false);   // same fixed-AGC mode as apply_gain()
    BK4819_WriteRegister(BK4819_REG_13, reg13);
}

uint8_t radio_get_gain_index(void)
{
    return s_gain_idx;
}

int8_t radio_get_gain_db(void)
{
    return GAIN_TABLE[s_gain_idx].gain_db;
}

// Gain stepping with reald's mistune semantics (ARDF_GainIncr/Decr with
// gARDFMistuneAddGainIdxSteps = 0): DOWN at minimum gain detunes the RX
// for extra attenuation; UP undoes the detune before raising gain.
void radio_gain_decr(void)
{
    if (s_gain_idx > 0)
    {
        s_gain_idx--;
        apply_gain();
    }
    else if (MISTUNE_SHIFT_HZ != 0u && !s_mistune)   // reald: only if configured
    {
        s_mistune = true;
        tune(s_freq_hz + MISTUNE_SHIFT_HZ);
    }
    // else: minimum gain (+ mistune, if any) — nothing left to do
}

void radio_gain_incr(void)
{
    if (s_mistune)
    {
        s_mistune = false;
        tune(s_freq_hz);
    }
    else if (s_gain_idx < RADIO_GAIN_MAX)
    {
        s_gain_idx++;
        apply_gain();
    }
}

bool radio_mistune_active(void)
{
    return s_mistune;
}

uint16_t radio_get_rssi(void)
{
    return BK4819_GetRSSI();
}

int16_t radio_get_rssi_dbm(int8_t band_correction_db)
{
    int16_t raw = (int16_t)BK4819_GetRSSI();
    return (int16_t)(raw / 2 - 160 + band_correction_db);
}

// --- Transmit -------------------------------------------------------------
// Bench-test TX: FM voice from the microphone on the current frequency, at a
// very low PA bias (platform TX_HW_PaBias). Sequence follows reald/armel
// RADIO_SetTxParameters + FUNCTION_Transmit, minus CTCSS/DTMF/compander.

// Amateur 2 m and 70 cm allocations only (Hz).
bool radio_tx_allowed(uint32_t hz)
{
    return (hz >= 144000000u && hz <= 146000000u)
        || (hz >= 430000000u && hz <= 440000000u);
}

radio_status_t radio_tx_start(void)
{
    if (!s_initialized || s_tx || !radio_tx_allowed(s_freq_hz))
        return RADIO_ERROR;

    const uint32_t f10 = s_freq_hz / 10u;   // BK4819 unit = 10 Hz
    // The 1.7 kHz setting is an RX-only DF filter; transmit narrow FM then.
    const BK4819_FilterBandwidth_t bw =
        (s_bw == RADIO_BW_WIDE) ? BK4819_FILTER_BW_WIDE : BK4819_FILTER_BW_NARROW;

    // Speaker off first: an open speaker next to the mic howls.
    AUDIO_AudioPathOff();
    BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, false);

    // Never modulate anything but the mic: tone generator and DTMF off.
    BK4819_WriteRegister(BK4819_REG_70, 0);
    BK4819_DisableDTMF();

    BK4819_SetFilterBandwidth(bw, false);
    BK4819_SetFrequency(f10);
    BK4819_SetCompander(0);
    // Mic sensitivity (REG_7D<5:0>), as reald/armel RADIO_SetupRegisters.
    BK4819_WriteRegister(BK4819_REG_7D, 0xE940 | (TX_HW_MicGain() & 0x3Fu));

    BK4819_PrepareTransmit();
    SYSTEM_DelayMs(10);
    BK4819_PickRXFilterPathBasedOnFrequency(f10);
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, true);
    SYSTEM_DelayMs(5);
    BK4819_SetupPowerAmplifier(TX_HW_PaBias(f10), f10);
    SYSTEM_DelayMs(10);
    BK4819_ExitSubAu();                      // no CTCSS/DCS

    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true);
    s_tx = true;
    return RADIO_OK;
}

void radio_tx_stop(void)
{
    if (!s_tx) return;

    // PA off first (reald RADIO_SetupRegisters).
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
    BK4819_SetupPowerAmplifier(0, 0);
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);

    // Clear any pending chip interrupt, bounded (reald loops unbounded).
    for (uint8_t i = 0; i < 20 && (BK4819_ReadRegister(BK4819_REG_0C) & 1u); i++)
    {
        BK4819_WriteRegister(BK4819_REG_02, 0);
        SYSTEM_DelayMs(1);
    }

    s_tx      = false;
    s_mistune = false;   // rx_setup retunes to the nominal frequency
    rx_setup();          // reprograms the squelch: state restarts closed
}

bool radio_is_transmitting(void)
{
    return s_tx;
}
