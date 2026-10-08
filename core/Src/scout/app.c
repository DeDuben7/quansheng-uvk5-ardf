/* scout-v2 application main loop.
 *
 * Polls the keyboard with simple debounce, dispatches presses, runs
 * the squelch gate and the audio-DF task with a smoothed RSSI, and
 * re-renders the current screen at ~10 Hz.
 *
 * Home screen keys: digits = frequency entry, UP/DOWN = gain, SIDE1 = DF
 * tone, MENU = settings menu (scout/menu.h: squelch, modulation, band,
 * scanner, info). Long presses do nothing on the home screen.
 *
 * F opens the TX PIN prompt (scout/tx.h); once unlocked, PTT transmits.
 */

#include <stdbool.h>
#include <stdint.h>

#include "scout/app.h"
#include "scout/home.h"
#include "scout/scan.h"
#include "scout/tx.h"
#include "scout/info.h"
#include "scout/gaincal.h"
#include "scout/menu.h"
#include "radio/radio.h"
#include "audio_df/audio_df.h"
#include "battery/battery.h"
#include "peripherals/keyboard.h"
#include "peripherals/system.h"
#include "peripherals/scheduler.h"

// --- Smoothed RSSI ---------------------------------------------------------
// 30-sample moving average at 100 Hz -> 300 ms window.
// Bigger window = steadier display + less audible tone jitter, at the cost
// of a small latency when sweeping the antenna.
#define RSSI_AVG_LEN 30u
static uint16_t s_rssi_buf[RSSI_AVG_LEN];
static uint8_t  s_rssi_idx;
static uint16_t s_rssi_avg;

static void rssi_avg_push(uint16_t rssi)
{
    s_rssi_buf[s_rssi_idx] = rssi;
    s_rssi_idx = (s_rssi_idx + 1) % RSSI_AVG_LEN;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < RSSI_AVG_LEN; i++) sum += s_rssi_buf[i];
    s_rssi_avg = (uint16_t)(sum / RSSI_AVG_LEN);
}

uint16_t scout_app_get_rssi_avg(void)
{
    return s_rssi_avg;
}

// --- Screens ----------------------------------------------------------------
typedef enum {
    SCREEN_HOME = 0,
    SCREEN_MENU,    // settings menu (MENU on the home screen)
    SCREEN_SCAN,    // Menu > Scanner
    SCREEN_INFO,    // Menu > Info: version + factory calibration readout
    SCREEN_GAINCAL  // gain sweep (MENU on the info screen)
} screen_t;

static screen_t s_screen = SCREEN_HOME;

// --- Key debouncing --------------------------------------------------------
// Track the current pressed key and how long it's been held in 10 ms ticks.
// Fire a "short" event on release if held < LONG_PRESS_TICKS, a "long" event
// once we cross that threshold.
#define LONG_PRESS_TICKS 50    // 500 ms

static KEY_Code_t s_pressed_key = KEY_INVALID;
static uint32_t   s_press_tick;
static bool       s_long_fired;

typedef enum {
    EVT_NONE = 0,
    EVT_SHORT,
    EVT_LONG
} key_event_t;

static key_event_t poll_key_event(KEY_Code_t* out_key)
{
    KEY_Code_t now = KEYBOARD_Poll();

    if (now != KEY_INVALID && s_pressed_key == KEY_INVALID)
    {
        // press start
        s_pressed_key = now;
        s_press_tick  = scout_tick_10ms();
        s_long_fired  = false;
    }
    else if (now == s_pressed_key && now != KEY_INVALID)
    {
        // still held
        if (!s_long_fired
            && (scout_tick_10ms() - s_press_tick) >= LONG_PRESS_TICKS)
        {
            s_long_fired = true;
            *out_key = s_pressed_key;
            return EVT_LONG;
        }
    }
    else if (now == KEY_INVALID && s_pressed_key != KEY_INVALID)
    {
        // released
        KEY_Code_t k = s_pressed_key;
        s_pressed_key = KEY_INVALID;
        if (!s_long_fired)
        {
            *out_key = k;
            return EVT_SHORT;
        }
    }

    return EVT_NONE;
}

// --- Dispatch --------------------------------------------------------------
static void dispatch_short(KEY_Code_t k)
{
    switch (k)
    {
        case KEY_0: case KEY_1: case KEY_2: case KEY_3: case KEY_4:
        case KEY_5: case KEY_6: case KEY_7: case KEY_8: case KEY_9:
            if (scout_tx_pin_active())
                scout_tx_pin_digit((uint8_t)k);
            else if (scout_home_input_digit((uint8_t)k))
            {
                scout_home_input_commit();
            }
            break;
        case KEY_EXIT:
            if (scout_tx_pin_active()) scout_tx_pin_cancel();
            else                       scout_home_input_clear();
            break;
        case KEY_F:
            // Not in the middle of a frequency entry.
            if (!scout_home_input_active())
                scout_tx_key_f();
            break;
        case KEY_UP:
            radio_gain_incr();
            break;
        case KEY_DOWN:
            radio_gain_decr();   // past minimum gain this engages mistune
            break;
        case KEY_SIDE1:
            if (audio_df_is_active())
            {
                audio_df_disable();
                // Give the speaker back to the radio: re-applies the
                // (squelch-gated) RX audio that audio_df left muted.
                radio_audio_hold(false);
            }
            else
            {
                radio_audio_hold(true);
                audio_df_enable();
            }
            break;
        case KEY_MENU:
            // Not while a frequency or PIN entry is in progress.
            if (!scout_home_input_active() && !scout_tx_pin_active())
            {
                menu_enter();
                s_screen = SCREEN_MENU;
            }
            break;
        default:
            break;
    }
}

static void menu_key(KEY_Code_t k)
{
    switch (menu_handle_key(k))
    {
        case MENU_EXIT_HOME:
            s_screen = SCREEN_HOME;
            break;
        case MENU_OPEN_SCANNER:
            scan_enter();
            s_screen = SCREEN_SCAN;
            break;
        case MENU_OPEN_INFO:
            s_screen = SCREEN_INFO;
            break;
        default:
            break;
    }
}

// --- Main loop -------------------------------------------------------------
void scout_app_run(void)
{
    uint32_t last_render_tick = 0;
    for (;;)
    {
        KEY_Code_t key = KEY_INVALID;
        key_event_t evt = poll_key_event(&key);

        // PTT: may start TX only from the plain home screen.
        scout_tx_task(s_screen == SCREEN_HOME
                      && !scout_home_input_active()
                      && !scout_tx_pin_active());

        // Keys are ignored while on air.
        if (evt != EVT_NONE && !scout_tx_active())
        {
            if (s_screen == SCREEN_MENU)
            {
                if (evt == EVT_SHORT)
                    menu_key(key);
            }
            else if (s_screen == SCREEN_INFO)
            {
                if (evt == EVT_SHORT && key == KEY_EXIT)
                    s_screen = SCREEN_MENU;
                else if (evt == EVT_SHORT && (key == KEY_UP || key == KEY_DOWN))
                    scout_info_toggle_page();
                else if (evt == EVT_SHORT && key == KEY_MENU)
                {
                    gaincal_enter();
                    s_screen = SCREEN_GAINCAL;
                }
            }
            else if (s_screen == SCREEN_GAINCAL)
            {
                if (evt == EVT_SHORT && !gaincal_handle_key(key))
                {
                    s_screen = SCREEN_INFO;
                    radio_squelch_resync();   // the sweep drove the receiver
                }
            }
            else if (s_screen == SCREEN_SCAN)
            {
                if (!scan_handle_key(key, evt == EVT_LONG))
                {
                    // Unconsumed keys: gain stays reachable so the
                    // "SAT! LOWER GAIN" hint is actionable mid-scan.
                    if (key == KEY_UP)        radio_gain_incr();
                    else if (key == KEY_DOWN) radio_gain_decr();
                }
            }
            else if (evt == EVT_SHORT)
                dispatch_short(key);   // home screen; long presses do nothing
        }

        // Squelch: open/mute the speaker on the chip's squelch interrupts.
        // Not during the scanner or gain sweeps: they own the receiver and
        // the speaker, and resync the squelch when they're done.
        if (s_screen != SCREEN_SCAN && s_screen != SCREEN_GAINCAL)
            radio_squelch_task();

        // Sample RSSI every loop iteration into the moving average
        // (meaningless while transmitting, so frozen then).
        if (!scout_tx_active())
            rssi_avg_push(radio_get_rssi());

        // Battery sampling (module self-limits to ~500 ms).
        battery_task();

        // Feed audio-DF.
        audio_df_task(s_rssi_avg);

        if (s_screen == SCREEN_GAINCAL)
            gaincal_task();

        // Scanner sweep progress + return-to-home when it finishes.
        if (s_screen == SCREEN_SCAN)
        {
            scan_task();
            if (scan_is_done())
                s_screen = SCREEN_HOME;
        }

        // Re-render at ~10 Hz.
        uint32_t now = scout_tick_10ms();
        if ((now - last_render_tick) >= 10)
        {
            last_render_tick = now;
            if (s_screen == SCREEN_MENU)
                menu_render();
            else if (s_screen == SCREEN_SCAN)
                scan_render();
            else if (s_screen == SCREEN_INFO)
                scout_info_render();
            else if (s_screen == SCREEN_GAINCAL)
                gaincal_render();
            else
                scout_home_render();
        }

        SYSTEM_DelayMs(10);
    }
}
