/* Gain sweep — see scout/gaincal.h.
 *
 * Non-blocking: gaincal_task() advances one small step per main-loop pass
 * (~10 ms), so the display keeps updating and EXIT aborts at any time.
 * For each candidate: write REG_13, wait for the RSSI detector to settle,
 * then average SAMPLES readings. The user's gain index is restored on exit.
 */

#include <string.h>

#include "external/printf/printf.h"
#include "scout/gaincal.h"
#include "radio/radio.h"
#include "st7565/st7565.h"
#include "ui/helper.h"
#include "peripherals/scheduler.h"

extern uint8_t gStatusLine[128];

// Candidate REG_13 values. Fields: [9:8] LNA-short [7:5] LNA [4:3] mixer
// [2:0] PGA. Numbered 0..28 on screen — keep this order, the results are
// read back by index.
static const uint16_t CANDIDATES[] = {
    // 0..16: the current gain table (radio.c GAIN_TABLE, reald's order)
    0x0000, 0x0109, 0x0148, 0x012A, 0x0104, 0x000E, 0x020D, 0x020F,
    0x0057, 0x008D, 0x00C6, 0x02FB, 0x03EB, 0x03D4, 0x03FC, 0x03FE, 0x03FF,
    // 17..23: LNA-only ladder, known monotonic (radio.c comment)
    0x031F, 0x033F, 0x035F, 0x037F, 0x039F, 0x03BF, 0x03DF,
    // 24..28: armel's V3 fox-hunt steps (app/foxhunt.c FOXHUNT_ATT_REG13)
    0x03DD, 0x03DB, 0x03D9, 0x0379, 0x0139,
};
#define N_CAND   ((uint8_t)(sizeof(CANDIDATES) / sizeof(CANDIDATES[0])))

#define SETTLE_TICKS 8u    // 80 ms after a gain change
#define SAMPLES      20u   // one per ~10 ms pass -> 200 ms average
#define PER_PAGE     14u   // 7 lines x 2 columns
#define N_PAGES      ((uint8_t)((N_CAND + PER_PAGE - 1u) / PER_PAGE))

static uint16_t s_result[N_CAND];
static uint8_t  s_idx;           // candidate being measured; N_CAND = done
static uint8_t  s_samples;
static uint32_t s_sum;
static uint32_t s_settle_start;
static uint8_t  s_page;

static void start_candidate(void)
{
    radio_set_raw_gain(CANDIDATES[s_idx]);
    s_samples      = 0;
    s_sum          = 0;
    s_settle_start = scout_tick_10ms();
}

void gaincal_enter(void)
{
    memset(s_result, 0, sizeof(s_result));
    s_idx  = 0;
    s_page = 0;
    start_candidate();
}

static bool running(void) { return s_idx < N_CAND; }

void gaincal_task(void)
{
    if (!running()) return;
    if ((scout_tick_10ms() - s_settle_start) < SETTLE_TICKS) return;

    s_sum += radio_get_rssi();
    if (++s_samples < SAMPLES) return;

    s_result[s_idx] = (uint16_t)(s_sum / SAMPLES);
    if (++s_idx < N_CAND)
        start_candidate();
    else
        radio_set_gain_index(radio_get_gain_index());   // done: user gain back
}

bool gaincal_handle_key(KEY_Code_t key)
{
    switch (key)
    {
        case KEY_EXIT:
            s_idx = N_CAND;                                 // stop, also mid-sweep
            radio_set_gain_index(radio_get_gain_index());   // restore user gain
            return false;
        case KEY_MENU:
            gaincal_enter();                                // measure again
            break;
        case KEY_UP:
            s_page = (uint8_t)((s_page + N_PAGES - 1u) % N_PAGES);
            break;
        case KEY_DOWN:
            s_page = (uint8_t)((s_page + 1u) % N_PAGES);
            break;
        default:
            break;
    }
    return true;
}

void gaincal_render(void)
{
    char buf[24];
    const uint32_t hz = radio_get_frequency();

    UI_DisplayClear();
    memset(gStatusLine, 0, sizeof(gStatusLine));
    if (running())
        snprintf(buf, sizeof(buf), "SWEEP %u/%u", s_idx + 1u, N_CAND);
    else
        snprintf(buf, sizeof(buf), "P%u/%u %lu.%03lu", s_page + 1u, N_PAGES,
                 (unsigned long)(hz / 1000000u),
                 (unsigned long)((hz % 1000000u) / 1000u));
    UI_PrintStringSmallBufferNormal(buf, gStatusLine);

    if (running())
    {
        UI_PrintStringSmallNormal("keep the carrier", 0, 128, 1);
        UI_PrintStringSmallNormal("steady...", 0, 128, 2);
        snprintf(buf, sizeof(buf), "RSSI now %u", radio_get_rssi());
        UI_PrintStringSmallNormal(buf, 0, 128, 4);
        UI_PrintStringSmallNormal("EXIT = abort", 0, 128, 6);
    }
    else
    {
        // Two columns of "nn rrr": candidate number, averaged raw RSSI.
        const uint8_t first = (uint8_t)(s_page * PER_PAGE);
        for (uint8_t line = 0; line < 7; line++)
        {
            const uint8_t a = (uint8_t)(first + line);
            const uint8_t b = (uint8_t)(a + 7u);
            char col_b[10] = "";
            if (b < N_CAND)
                snprintf(col_b, sizeof(col_b), "%2u %3u", b, s_result[b]);
            if (a < N_CAND)
                snprintf(buf, sizeof(buf), "%2u %3u   %s", a, s_result[a], col_b);
            else
                buf[0] = '\0';
            UI_PrintStringSmallNormal(buf, 0, 128, line);
        }
    }

    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}
