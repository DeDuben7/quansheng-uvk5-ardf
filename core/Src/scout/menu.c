/* Settings menu — see scout/menu.h.
 *
 * Layout (128 x 64, small font = 7 px per character):
 *
 *   MENU                       v1.0    [status strip]
 *   ▓Squelch                   3▓      list mode: selected row inverted
 *    Modulatie                FM
 *    Band                    2 m
 *    Verlichting            auto
 *    Scanner                   >
 *    Info                      >
 *
 *   MENU=OK EXIT=terug                 [hint, line 6]
 *
 * In edit mode only the value is inverted and the hint changes.
 *
 * The two band presets live here: the menu is the only place that swaps
 * band. Each band keeps its last frequency while you're on the other one.
 * Which band we're on is read from the radio's frequency, because the home
 * screen's frequency entry can jump bands without the menu.
 */

#include <stdbool.h>
#include <string.h>

#include "external/printf/printf.h"
#include "radio/radio.h"
#include "scout/config.h"
#include "scout/light.h"
#include "scout/menu.h"
#include "scout/version.h"
#include "st7565/st7565.h"
#include "ui/helper.h"

extern uint8_t gStatusLine[128];

#define CHAR_W      7u      // small font advance
#define LABEL_X     4u
#define VALUE_RIGHT 124u    // values end here

typedef enum {
    ITEM_SQUELCH = 0,
    ITEM_MODULATION,
    ITEM_BAND,
    ITEM_LIGHT,
    ITEM_SCANNER,
    ITEM_INFO,
    ITEM_COUNT
} item_t;

static const char *const LABELS[ITEM_COUNT] = {
    "Squelch", "Modulatie", "Band", "Verlichting", "Scanner", "Info"
};

static uint8_t s_cursor;
static bool    s_editing;
static uint8_t s_saved;     // value before editing, for EXIT = cancel

// --- Band presets ------------------------------------------------------------
#define BAND_SPLIT_HZ 300000000u   // below: 2 m, above: 70 cm (as scan.c)

static uint32_t s_band_2m_hz   = SCOUT_BOOT_FREQ_HZ;
static uint32_t s_band_70cm_hz = SCOUT_70CM_PRESET_HZ;

static bool on_2m(void)
{
    return radio_get_frequency() < BAND_SPLIT_HZ;
}

static void swap_band(void)
{
    const uint32_t hz = radio_get_frequency();
    if (hz < BAND_SPLIT_HZ)
    {
        s_band_2m_hz = hz;
        radio_set_frequency(s_band_70cm_hz);
    }
    else
    {
        s_band_70cm_hz = hz;
        radio_set_frequency(s_band_2m_hz);
    }
}

// --- Item values ---------------------------------------------------------------
static uint8_t value_get(item_t item)
{
    switch (item)
    {
        case ITEM_SQUELCH:    return radio_get_squelch();
        case ITEM_MODULATION: return (uint8_t)radio_get_modulation();
        case ITEM_BAND:       return on_2m() ? 0u : 1u;
        case ITEM_LIGHT:      return (uint8_t)light_get_mode();
        default:              return 0;
    }
}

static void value_set(item_t item, uint8_t v)
{
    switch (item)
    {
        case ITEM_SQUELCH:
            radio_set_squelch(v);
            break;
        case ITEM_MODULATION:
            radio_set_modulation(v ? RADIO_MOD_AM : RADIO_MOD_FM);
            break;
        case ITEM_BAND:
            if ((v == 0) != on_2m())
                swap_band();
            break;
        case ITEM_LIGHT:
            light_set_mode((light_mode_t)v);
            break;
        default:
            break;
    }
}

// UP/DOWN in edit mode. Squelch steps and stops at 0 / 9; the backlight
// cycles auto -> aan -> uit; the two-valued items toggle on either key.
static void value_step(item_t item, bool up)
{
    const uint8_t v = value_get(item);
    if (item == ITEM_SQUELCH)
    {
        if (up && v < RADIO_SQL_MAX) value_set(item, (uint8_t)(v + 1));
        if (!up && v > 0)            value_set(item, (uint8_t)(v - 1));
    }
    else if (item == ITEM_LIGHT)
    {
        const uint8_t n = LIGHT_MODE_COUNT;
        value_set(item, (uint8_t)((v + (up ? 1u : n - 1u)) % n));
    }
    else
    {
        value_set(item, v ? 0u : 1u);
    }
}

static const char *value_text(item_t item, char *buf, size_t size)
{
    switch (item)
    {
        case ITEM_SQUELCH:
        {
            const uint8_t level = radio_get_squelch();
            if (level == 0) return "uit";
            snprintf(buf, size, "%u", level);
            return buf;
        }
        case ITEM_MODULATION: return (radio_get_modulation() == RADIO_MOD_AM) ? "AM" : "FM";
        case ITEM_BAND:       return on_2m() ? "2 m" : "70 cm";
        case ITEM_LIGHT:
            switch (light_get_mode())
            {
                case LIGHT_ON:  return "aan";
                case LIGHT_OFF: return "uit";
                default:        return "auto";
            }
        default:              return ">";
    }
}

static bool is_value_item(item_t item)
{
    return item == ITEM_SQUELCH || item == ITEM_MODULATION || item == ITEM_BAND
        || item == ITEM_LIGHT;
}

// --- Public --------------------------------------------------------------------
void menu_enter(void)
{
    s_editing = false;
}

menu_result_t menu_handle_key(KEY_Code_t key)
{
    const item_t item = (item_t)s_cursor;

    if (s_editing)
    {
        switch (key)
        {
            case KEY_UP:   value_step(item, true);  break;
            case KEY_DOWN: value_step(item, false); break;
            case KEY_MENU: s_editing = false;       break;
            case KEY_EXIT:
                value_set(item, s_saved);
                s_editing = false;
                break;
            default: break;
        }
        return MENU_STAY;
    }

    switch (key)
    {
        case KEY_UP:
            s_cursor = (uint8_t)((s_cursor + ITEM_COUNT - 1) % ITEM_COUNT);
            return MENU_STAY;
        case KEY_DOWN:
            s_cursor = (uint8_t)((s_cursor + 1) % ITEM_COUNT);
            return MENU_STAY;
        case KEY_MENU:
            if (is_value_item(item))
            {
                s_saved   = value_get(item);
                s_editing = true;
                return MENU_STAY;
            }
            return (item == ITEM_SCANNER) ? MENU_OPEN_SCANNER : MENU_OPEN_INFO;
        case KEY_EXIT:
            return MENU_EXIT_HOME;
        default:
            return MENU_STAY;
    }
}

static void invert(uint8_t line, uint8_t x0, uint8_t x1)
{
    for (uint8_t x = x0; x <= x1 && x < 128; x++)
        gFrameBuffer[line][x] ^= 0xFF;
}

void menu_render(void)
{
    char buf[8];

    UI_DisplayClear();
    memset(gStatusLine, 0, sizeof(gStatusLine));
    UI_PrintStringSmallBufferNormal("MENU", gStatusLine);
    const char *ver = scout_version_label();
    UI_PrintStringSmallBufferNormal(ver, gStatusLine + 128 - CHAR_W * strlen(ver));

    for (uint8_t i = 0; i < ITEM_COUNT; i++)
    {
        const char *value = value_text((item_t)i, buf, sizeof(buf));
        const uint8_t value_x = (uint8_t)(VALUE_RIGHT - CHAR_W * strlen(value));
        UI_PrintStringSmallNormal(LABELS[i], LABEL_X, 0, i);
        UI_PrintStringSmallNormal(value, value_x, 0, i);

        if (i == s_cursor)
        {
            if (s_editing) invert(i, (uint8_t)(value_x - 2), 127);
            else           invert(i, 0, 127);
        }
    }

    UI_PrintStringSmallNormal(s_editing ? "UP/DOWN  MENU=OK" : "MENU=OK EXIT=terug", 0, 128, 6);

    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}
