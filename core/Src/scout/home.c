/* Home screen renderer for scout-v2.
 *
 * Layout (128 x 64):
 *
 *   AGGER MARTINI GROEP              [status strip, page 0]
 *   ┌────────────────────────────────┐
 *   │       144.5000 MHz              │   big frequency
 *   │       FM N G:07 SQ3 T           │   modulation / bw / gain / squelch / DF
 *   │       RSSI: 128                 │   numeric RSSI
 *   │  ▓▓▓▓▓▓▓▓▓▓▓▓░░░░░░░░░░░░░░     │   bargraph + peak marker
 *   └────────────────────────────────┘
 */

#include <string.h>

#include "external/printf/printf.h"
#include "scout/home.h"
#include "scout/app.h"
#include "scout/tx.h"
#include "radio/radio.h"
#include "audio_df/audio_df.h"
#include "battery/battery.h"
#include "st7565/st7565.h"
#include "ui/helper.h"
#include "common.h"

extern uint8_t gStatusLine[128];

#define FREQ_INPUT_LEN 6
static char    s_input[FREQ_INPUT_LEN + 1];
static uint8_t s_input_len;

// Track peak RSSI for the bargraph peak-hold marker.
static uint16_t s_rssi_peak;
static uint8_t  s_rssi_peak_age_ticks;

static void draw_status_line(void)
{
    memset(gStatusLine, 0, sizeof(gStatusLine));
    // "TX" marks that transmit is unlocked (PIN entered this power-on).
    UI_PrintStringSmallBufferNormal(scout_tx_unlocked() ? "AMG SCOUT TX" : "AMG SCOUT",
                                    gStatusLine);

    // Battery percentage, right-aligned (small font = 7 px per char).
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", battery_get_percent());
    UI_PrintStringSmallBufferNormal(buf, gStatusLine + 128 - 7 * strlen(buf));
}

static void draw_frequency(void)
{
    char buf[12];
    if (scout_home_input_active())
    {
        // Show the in-progress entry: e.g. "145.5  "
        snprintf(buf, sizeof(buf), "%c%c%c.%c%c%c",
                 s_input[0] ? s_input[0] : '_',
                 s_input[1] ? s_input[1] : '_',
                 s_input[2] ? s_input[2] : '_',
                 s_input[3] ? s_input[3] : '_',
                 s_input[4] ? s_input[4] : '_',
                 s_input[5] ? s_input[5] : '_');
    }
    else
    {
        uint32_t hz = radio_get_frequency();
        uint32_t mhz_int = hz / 1000000u;
        uint32_t mhz_frac = (hz % 1000000u) / 100u;   // 4 fractional digits
        snprintf(buf, sizeof(buf), "%3lu.%04lu",
                 (unsigned long)mhz_int, (unsigned long)mhz_frac);
    }
    UI_PrintString(buf, 0, 128, 0, 10);  // big font, lines 0+1
}

static void draw_modulation_line(void)
{
    char buf[24];
    const char* mod  = (radio_get_modulation() == RADIO_MOD_AM) ? "AM" : "FM";
    const char* bw;
    switch (radio_get_bandwidth())
    {
        case RADIO_BW_WIDE:       bw = "W";   break;
        case RADIO_BW_NARROW:     bw = "N";   break;
        case RADIO_BW_NARROW_1K7: bw = "U1k7"; break;
        default:                  bw = "?";   break;
    }
    const char* df = audio_df_is_active() ? "T" : "-";
    // Gain shown as table index 0..16 like reald's firmware — the dB
    // labels in the table are another chip's calibration and don't match
    // this radio's real attenuation. '*' = mistune active.
    snprintf(buf, sizeof(buf), "%s %s G:%02u%s SQ%u %s",
             mod, bw, radio_get_gain_index(),
             radio_mistune_active() ? "*" : "", radio_get_squelch(), df);
    UI_PrintStringSmallNormal(buf, 0, 128, 3);
}

static void draw_rssi(uint16_t rssi, uint16_t avg)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "RSSI %3u  avg %3u", rssi, avg);
    UI_PrintStringSmallNormal(buf, 0, 128, 4);
}

// Calibrated for the UV-K5 at 145.5 MHz: noise floor ~45 raw, strong fox
// saturates around 200. Map below floor to empty, above ceiling to full.
#define BAR_RSSI_FLOOR 40u
#define BAR_RSSI_CEIL  200u

static uint8_t rssi_to_bar_px(uint16_t rssi)
{
    if (rssi <= BAR_RSSI_FLOOR) return 0;
    if (rssi >= BAR_RSSI_CEIL)  return 128;
    return (uint8_t)(((uint32_t)(rssi - BAR_RSSI_FLOOR) * 128u)
                     / (BAR_RSSI_CEIL - BAR_RSSI_FLOOR));
}

static void draw_bargraph(uint16_t rssi)
{
    if (rssi > s_rssi_peak)
    {
        s_rssi_peak = rssi;
        s_rssi_peak_age_ticks = 0;
    }
    else if (++s_rssi_peak_age_ticks > 20)   // ~2 s with 100 ms render rate
    {
        s_rssi_peak = rssi;
        s_rssi_peak_age_ticks = 0;
    }

    uint8_t cur_px  = rssi_to_bar_px(rssi);
    uint8_t peak_px = rssi_to_bar_px(s_rssi_peak);

    uint8_t* row = gFrameBuffer[5];   // bar lives in framebuffer page 5
    for (uint8_t x = 0; x < 128; x++)
    {
        // Top/bottom rail = empty-tube outline. Full byte = solid fill.
        row[x] = (x < cur_px) ? 0xFF : 0x81;
    }
    if (peak_px > 0 && peak_px <= 128)
    {
        row[peak_px - 1] = 0xFF;
    }
}

// Lines 3-4 while the TX PIN prompt is open.
static void draw_pin_prompt(void)
{
    char buf[24];
    char stars[SCOUT_TX_PIN_LEN + 1];
    for (uint8_t i = 0; i < SCOUT_TX_PIN_LEN; i++)
        stars[i] = (i < scout_tx_pin_len()) ? '*' : '_';
    stars[SCOUT_TX_PIN_LEN] = '\0';
    snprintf(buf, sizeof(buf), "TX PIN: %s", stars);
    UI_PrintStringSmallNormal(buf, 0, 128, 3);
    UI_PrintStringSmallNormal("EXIT = cancel", 0, 128, 4);
}

// Lines 3-4 while transmitting.
static void draw_tx(void)
{
    char buf[24];
    UI_PrintStringSmallNormal(">>>  TX  <<<", 0, 128, 3);
    snprintf(buf, sizeof(buf), "ON AIR  %2us / 60s", scout_tx_elapsed_s());
    UI_PrintStringSmallNormal(buf, 0, 128, 4);
}

void scout_home_render(void)
{
    UI_DisplayClear();
    draw_status_line();
    draw_frequency();
    uint16_t rssi = radio_get_rssi();
    uint16_t avg  = scout_app_get_rssi_avg();
    if (scout_tx_active())
    {
        draw_tx();
    }
    else if (scout_tx_pin_active())
    {
        draw_pin_prompt();
    }
    else
    {
        draw_modulation_line();
        // A TX status message temporarily replaces the RSSI readout.
        const char *msg = scout_tx_message();
        if (msg != NULL) UI_PrintStringSmallNormal(msg, 0, 128, 4);
        else             draw_rssi(rssi, avg);
    }
    draw_bargraph(scout_tx_active() ? 0 : avg);   // follows the average — less jitter
    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}

bool scout_home_input_digit(uint8_t digit)
{
    if (digit > 9) return false;
    if (s_input_len >= FREQ_INPUT_LEN) return true;
    s_input[s_input_len++] = (char)('0' + digit);
    s_input[s_input_len] = '\0';
    return s_input_len == FREQ_INPUT_LEN;
}

void scout_home_input_clear(void)
{
    s_input_len = 0;
    s_input[0] = '\0';
}

bool scout_home_input_active(void)
{
    return s_input_len > 0;
}

void scout_home_input_commit(void)
{
    if (s_input_len != FREQ_INPUT_LEN) return;
    // s_input is "MMMKKK" (e.g. 145500 -> 145.500 MHz). Convert to Hz.
    uint32_t mhz   = (uint32_t)(s_input[0] - '0') * 100
                   + (uint32_t)(s_input[1] - '0') *  10
                   + (uint32_t)(s_input[2] - '0');
    uint32_t khz   = (uint32_t)(s_input[3] - '0') * 100
                   + (uint32_t)(s_input[4] - '0') *  10
                   + (uint32_t)(s_input[5] - '0');
    uint32_t hz = mhz * 1000000u + khz * 1000u;
    radio_set_frequency(hz);
    scout_home_input_clear();
}
