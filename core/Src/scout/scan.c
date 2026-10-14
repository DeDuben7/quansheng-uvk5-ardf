/* scan implementation. See scan.h for the module contract. */

#include <stddef.h>
#include <string.h>

#include "external/printf/printf.h"
#include "scout/scan.h"
#include "radio/radio.h"
#include "audio_df/audio_df.h"
#include "bk4819/bk4819.h"
#include "peripherals/audio_path.h"
#include "st7565/st7565.h"
#include "ui/helper.h"

#define STEP_HZ        12500u
#define NUM_CANDIDATES 5u
#define MERGE_HZ       25000u   // candidates this close are one signal
#define SATURATED_RSSI 250u

// Settle wait per step. After radio_tune_raw the glitch indicator (REG_63)
// reads high until the receiver has settled on the new channel; read RSSI
// once it drops below SETTLE_GLITCH, as armel's V3 spectrum scanner does.
// A busy channel can keep it high, so give up after SETTLE_MAX_POLLS
// checks (one register read each, ~0.1 ms) and read anyway.
#define SETTLE_GLITCH    200u
#define SETTLE_MAX_POLLS 50u

// Sweep ranges (spec): NL fox-hunt territory, not the full bands.
#define RANGE_2M_LO_HZ  144000000u
#define RANGE_2M_HI_HZ  146000000u
#define RANGE_70_LO_HZ  433000000u
#define RANGE_70_HI_HZ  435000000u

typedef enum {
    SCAN_SWEEPING = 0,
    SCAN_RESULTS
} scan_state_t;

typedef struct {
    uint32_t hz;
    uint16_t rssi;
} candidate_t;

static scan_state_t s_state;
static bool         s_done;
static uint8_t      s_polls;          // settle checks for the current step

// Radio state saved on entry, restored on exit/tune.
static uint32_t          s_saved_freq_hz;
static radio_bandwidth_t s_saved_bw;
static bool              s_df_was_active;

static uint32_t s_lo_hz, s_hi_hz;   // sweep range for the current band
static uint32_t s_step;             // index of the step currently settling
static uint32_t s_total_steps;

static candidate_t s_cand[NUM_CANDIDATES];
static uint8_t     s_cand_count;

static void start_sweep(void)
{
    s_cand_count = 0;
    memset(s_cand, 0, sizeof(s_cand));
    s_step  = 0;
    s_state = SCAN_SWEEPING;
    // Tune the first step now; scan_task reads its RSSI once it settled.
    radio_tune_raw(s_lo_hz);
    s_polls = 0;
}

// Insert (hz, rssi) into the top-5, strongest first. A reading within
// MERGE_HZ of an existing candidate updates that candidate instead of
// taking a second slot (one real signal must not fill two rows).
static void consider_candidate(uint32_t hz, uint16_t rssi)
{
    for (uint8_t i = 0; i < s_cand_count; i++)
    {
        const uint32_t d = (hz > s_cand[i].hz) ? hz - s_cand[i].hz
                                               : s_cand[i].hz - hz;
        if (d <= MERGE_HZ)
        {
            if (rssi > s_cand[i].rssi)
            {
                s_cand[i].hz   = hz;
                s_cand[i].rssi = rssi;
                // Re-sort upwards: the improved entry may outrank others.
                while (i > 0 && s_cand[i].rssi > s_cand[i - 1].rssi)
                {
                    const candidate_t tmp = s_cand[i - 1];
                    s_cand[i - 1] = s_cand[i];
                    s_cand[i] = tmp;
                    i--;
                }
            }
            return;
        }
    }

    // Not near an existing candidate: insertion sort into the array.
    uint8_t pos = s_cand_count;
    while (pos > 0 && rssi > s_cand[pos - 1].rssi)
        pos--;
    if (pos >= NUM_CANDIDATES)
        return;   // weaker than every kept candidate

    const uint8_t last = (s_cand_count < NUM_CANDIDATES) ? s_cand_count
                                                         : NUM_CANDIDATES - 1;
    for (uint8_t i = last; i > pos; i--)
        s_cand[i] = s_cand[i - 1];
    s_cand[pos].hz   = hz;
    s_cand[pos].rssi = rssi;
    if (s_cand_count < NUM_CANDIDATES)
        s_cand_count++;
}

// Restore everything scan_enter() changed, tuned to `hz`.
static void restore_radio(uint32_t hz)
{
    radio_set_bandwidth(s_saved_bw);
    radio_set_frequency(hz);
    // Re-assert the AF path + speaker route (scan muted them).
    radio_set_modulation(radio_get_modulation());
    if (s_df_was_active)
        audio_df_enable();
}

void scan_enter(void)
{
    s_saved_freq_hz = radio_get_frequency();
    s_saved_bw      = radio_get_bandwidth();
    s_df_was_active = audio_df_is_active();
    if (s_df_was_active)
        audio_df_disable();
    AUDIO_AudioPathOff();   // don't blast sweep noise at the user

    // Pick the sweep range from the band we're on, and let the radio
    // pick the matching RX filter path once for the whole sweep.
    if (s_saved_freq_hz >= 300000000u)
    {
        s_lo_hz = RANGE_70_LO_HZ;
        s_hi_hz = RANGE_70_HI_HZ;
    }
    else
    {
        s_lo_hz = RANGE_2M_LO_HZ;
        s_hi_hz = RANGE_2M_HI_HZ;
    }
    s_total_steps = (s_hi_hz - s_lo_hz) / STEP_HZ + 1;
    radio_set_frequency(s_lo_hz);           // filter path + PLL
    radio_set_bandwidth(RADIO_BW_NARROW);   // steps tile the band at 12.5 k

    s_done = false;
    start_sweep();
}

void scan_task(void)
{
    if (s_state != SCAN_SWEEPING || s_done)
        return;

    // Not settled yet: check again on the next call.
    if (BK4819_GetGlitchIndicator() >= SETTLE_GLITCH
        && ++s_polls < SETTLE_MAX_POLLS)
        return;

    // The first RSSI read after a retune can still be moving; keep the
    // second (armel does the same).
    (void)radio_get_rssi();
    consider_candidate(s_lo_hz + s_step * STEP_HZ, radio_get_rssi());

    s_step++;
    if (s_step >= s_total_steps)
        s_state = SCAN_RESULTS;
    else
    {
        radio_tune_raw(s_lo_hz + s_step * STEP_HZ);
        s_polls = 0;
    }
}

bool scan_is_sweeping(void)
{
    return s_state == SCAN_SWEEPING && !s_done;
}

bool scan_handle_key(KEY_Code_t key, bool is_long)
{
    (void)is_long;

    if (key == KEY_EXIT)
    {
        restore_radio(s_saved_freq_hz);
        s_done = true;
        return true;
    }
    if (key == KEY_STAR)
    {
        start_sweep();
        return true;
    }
    if (s_state == SCAN_RESULTS && key >= KEY_1 && key <= KEY_5)
    {
        const uint8_t idx = (uint8_t)(key - KEY_1);
        if (idx < s_cand_count)
        {
            restore_radio(s_cand[idx].hz);
            s_done = true;
        }
        return true;
    }
    return false;
}

bool scan_is_done(void)
{
    return s_done;
}

static bool all_saturated(void)
{
    if (s_cand_count == 0)
        return false;
    for (uint8_t i = 0; i < s_cand_count; i++)
        if (s_cand[i].rssi < SATURATED_RSSI)
            return false;
    return true;
}

void scan_render(void)
{
    char buf[24];

    UI_DisplayClear();
    // Own status strip: the scanner is opened from the menu, whose header
    // would otherwise stay on screen.
    memset(gStatusLine, 0, sizeof(gStatusLine));
    UI_PrintStringSmallBufferNormal("SCANNER", gStatusLine);

    if (s_state == SCAN_SWEEPING)
    {
        const uint32_t pct = s_step * 100u / s_total_steps;
        snprintf(buf, sizeof(buf), "SCANNING %lu%%", (unsigned long)pct);
        UI_PrintStringSmallNormal(buf, 0, 128, 0);
        snprintf(buf, sizeof(buf), "%3lu.%01lu - %3lu.%01lu",
                 (unsigned long)(s_lo_hz / 1000000u),
                 (unsigned long)((s_lo_hz % 1000000u) / 100000u),
                 (unsigned long)(s_hi_hz / 1000000u),
                 (unsigned long)((s_hi_hz % 1000000u) / 100000u));
        UI_PrintStringSmallNormal(buf, 0, 128, 3);
    }
    else if (s_cand_count == 0)
    {
        UI_PrintStringSmallNormal("NO SIGNALS", 0, 128, 2);
        UI_PrintStringSmallNormal("* RESCAN EXIT BACK", 0, 128, 4);
    }
    else
    {
        UI_PrintStringSmallNormal(all_saturated() ? "SAT! LOWER GAIN"
                                                  : "TUNE 1-5  * AGAIN",
                                  0, 128, 0);

        for (uint8_t i = 0; i < s_cand_count; i++)
        {
            snprintf(buf, sizeof(buf), "%u %3lu.%04lu",
                     (unsigned)(i + 1),
                     (unsigned long)(s_cand[i].hz / 1000000u),
                     (unsigned long)((s_cand[i].hz % 1000000u) / 100u));
            // End=0 skips UI_PrintStringSmall's centering: left-align the
            // row (10 chars = 70 px) so it stays clear of the bar at x>=80.
            UI_PrintStringSmallNormal(buf, 0, 0, (uint8_t)(i + 1));

            // Mini-bar in columns 80..127, proportional to raw RSSI.
            const uint8_t len = (uint8_t)(((uint32_t)s_cand[i].rssi * 48u)
                                          / 255u);
            uint8_t *row = gFrameBuffer[i + 1];
            for (uint8_t x = 0; x < len && (80 + x) < 128; x++)
                row[80 + x] |= 0x3C;
        }
        UI_PrintStringSmallNormal("EXIT BACK", 0, 128, 6);
    }

    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}
