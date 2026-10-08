/* Info screen (Menu > Info), two pages switched with UP/DOWN:
 *
 *  1. Version + commit (status line), factory calibration — what this radio's
 *     factory calibration contains and what the firmware derives from it, so
 *     it can be checked against a meter.
 *  2. SQUELCH — live squelch readout for measuring with a signal generator:
 *     the chip's RSSI, noise and glitch indicators next to the open (o) and
 *     close (c) thresholds actually programmed in the BK4819, and whether
 *     the squelch is open. Noise and glitch open when BELOW their threshold,
 *     RSSI when above it.
 */

#include <string.h>

#include "external/printf/printf.h"
#include "scout/info.h"
#include "bk4819/bk4819.h"
#include "bk4819/bk4819_regs.h"
#include "calib/calib.h"
#include "radio/radio.h"
#include "radio/tx_hw.h"
#include "battery/battery.h"
#include "st7565/st7565.h"
#include "ui/helper.h"
#include "scout/version.h"

extern uint8_t gStatusLine[128];

static bool s_squelch_page;

void scout_info_toggle_page(void)
{
    s_squelch_page = !s_squelch_page;
}

static void line_triplet(const char *label, uint8_t band, uint8_t line)
{
    char buf[24];
    uint8_t t[3];
    if (calib_tx_low_triplet(band, t))
        snprintf(buf, sizeof(buf), "%s LOW %u %u %u", label, t[0], t[1], t[2]);
    else
        snprintf(buf, sizeof(buf), "%s LOW blank", label);
    UI_PrintStringSmallNormal(buf, 0, 128, line);
}

static void render_factory_cal(void)
{
    char buf[24];

    snprintf(buf, sizeof(buf), "%s %s", scout_version_label(), scout_git_hash());
    UI_PrintStringSmallBufferNormal(buf, gStatusLine);

    const uint16_t cal = calib_battery_adc760();
    if (cal) snprintf(buf, sizeof(buf), "BAT cal %u", cal);
    else     snprintf(buf, sizeof(buf), "BAT cal blank");
    UI_PrintStringSmallNormal(buf, 0, 128, 0);

    const uint16_t v = battery_get_voltage_10mV();
    snprintf(buf, sizeof(buf), "BAT %u.%02uV %u%%", v / 100u, v % 100u, battery_get_percent());
    UI_PrintStringSmallNormal(buf, 0, 128, 1);

    line_triplet("2m", CALIB_BAND_137, 2);
    line_triplet("70", CALIB_BAND_400, 3);

    const uint32_t hz = radio_get_frequency();
    snprintf(buf, sizeof(buf), "%lu.%04lu", (unsigned long)(hz / 1000000u),
             (unsigned long)((hz % 1000000u) / 100u));
    UI_PrintStringSmallNormal(buf, 0, 128, 4);

    if (radio_tx_allowed(hz))
        snprintf(buf, sizeof(buf), "TX bias here %u", TX_HW_PaBias(hz / 10u));
    else
        snprintf(buf, sizeof(buf), "TX: not ham band");
    UI_PrintStringSmallNormal(buf, 0, 128, 5);
}

static void render_squelch(void)
{
    char buf[24];

    snprintf(buf, sizeof(buf), "SQUELCH %s", scout_version_label());
    UI_PrintStringSmallBufferNormal(buf, gStatusLine);

    const uint32_t hz    = radio_get_frequency();
    const uint8_t  level = radio_get_squelch();
    calib_squelch_t t;
    const char *state;
    if (level == 0)                                 state = "uit";
    else if (!calib_squelch(hz / 10u, level, &t))   state = "geen tabel";
    else if (radio_squelch_is_open())               state = "OPEN";
    else                                            state = "dicht";
    snprintf(buf, sizeof(buf), "SQ%u %s", level, state);
    UI_PrintStringSmallNormal(buf, 0, 0, 0);

    // Thresholds as programmed by BK4819_SetupSquelch.
    const uint16_t r78 = BK4819_ReadRegister(BK4819_REG_78);   // open RSSI << 8 | close
    const uint16_t r4f = BK4819_ReadRegister(BK4819_REG_4F);   // close noise << 8 | open
    const uint16_t r4e = BK4819_ReadRegister(BK4819_REG_4E);   // ... | open glitch
    const uint16_t r4d = BK4819_ReadRegister(BK4819_REG_4D);   // ... | close glitch

    snprintf(buf, sizeof(buf), "RSSI%4u o%3u c%3u", radio_get_rssi(),
             (unsigned)(r78 >> 8), (unsigned)(r78 & 0xFFu));
    UI_PrintStringSmallNormal(buf, 0, 0, 1);
    snprintf(buf, sizeof(buf), "NOIS%4u o%3u c%3u", BK4819_GetExNoiceIndicator(),
             (unsigned)(r4f & 0x7Fu), (unsigned)((r4f >> 8) & 0x7Fu));
    UI_PrintStringSmallNormal(buf, 0, 0, 2);
    snprintf(buf, sizeof(buf), "GLIT%4u o%3u c%3u", BK4819_GetGlitchIndicator(),
             (unsigned)(r4e & 0xFFu), (unsigned)(r4d & 0xFFu));
    UI_PrintStringSmallNormal(buf, 0, 0, 3);

    snprintf(buf, sizeof(buf), "%lu.%04lu G:%02u", (unsigned long)(hz / 1000000u),
             (unsigned long)((hz % 1000000u) / 100u), radio_get_gain_index());
    UI_PrintStringSmallNormal(buf, 0, 0, 4);
}

void scout_info_render(void)
{
    UI_DisplayClear();
    memset(gStatusLine, 0, sizeof(gStatusLine));

    if (s_squelch_page) render_squelch();
    else                render_factory_cal();

    UI_PrintStringSmallNormal("UP/DOWN  MENU=gain", 0, 128, 6);

    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}
