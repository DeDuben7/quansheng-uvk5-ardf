/* UV-K5 V3 (PY32F071) — Phase 4 Task 3 (FINAL): boot into the full scout
 * application.
 *
 * This replaces all Phase 1-3 test scaffolding (flashlight heartbeat blink,
 * ad-hoc RSSI/BATT/KEY status lines) with the real boot sequence. Every
 * driver below is already ported and proven individually in Phases 0-3;
 * this file only wires them together in V1's exact order and hands off to
 * the same application entry point V1 uses, so the scout app owns its own
 * home screen / key dispatch / audio-DF / scanner loop from here on.
 *
 * Boot order mirrors V1's Src/main.c Main() exactly (read in full for this
 * task), substituting V3 symbol names where drivers were renamed:
 *
 *   V1                                   V3
 *   -------------------------------------------------------------------
 *   SYSTICK_Init()                       SCHEDULER_Init()  (10 ms tick)
 *   BOARD_Init()                         SPI_LCD_Init() + ST7565_Init()
 *   BK4819_Init()                        BK4819_Init()            (same)
 *   backlight_init()+backlight_turn_on() BACKLIGHT_On()
 *   UI_DisplayWelcome()                  UI_DisplayWelcome()      (same)
 *   radio_init(&radio_cfg)               radio_init(&radio_cfg)   (same)
 *   calib_battery_adc760() -> battery_init   (same; V1 reads EEPROM,
 *                                         V3 the SPI flash — core/calib)
 *   audio_df_init(&df_cfg)               audio_df_init(&df_cfg)   (same)
 *   SYSTEM_DelayMs(1500)                 DelayMs(1500)
 *   scout_app_run()                      scout_app_run()          (same)
 *
 * ADC_Init() (one-time battery-ADC front-end setup; V3 has no EEPROM-driven
 * calibration read to place it next to, unlike V1) is folded in alongside
 * SCHEDULER_Init() at the very top, before any driver that might contend
 * for time.
 *
 * scout_app_run() (Src/scout/app.c, carried over verbatim from V1 — see
 * Inc/scout/app.h: "main.c hands off here after hardware init. Never
 * returns.") owns the application's main loop: keyboard dispatch, the
 * smoothed-RSSI audio-DF feed, battery_task(), the scanner, and rendering
 * the home screen at ~10 Hz. It never returns; the idle loop below is a
 * "boot ok but app returned" safety net that should never execute.
 */
#include <stdint.h>

#include "py32f0xx.h"          /* SystemCoreClock, SystemCoreClockUpdate() */
#include "peripherals/delay.h"
#include "peripherals/spi_lcd.h"
#include "peripherals/adc.h"
#include "peripherals/backlight.h"
#include "st7565/st7565.h"
#include "ui/welcome.h"
#include "bk4819/bk4819.h"
#include "radio/radio.h"
#include "battery/battery.h"
#include "audio_df/audio_df.h"
#include "scout/app.h"
#include "scheduler.h"
#include "calib/calib.h"
#include "scout/config.h"

/* Adapts ADC_GetBatteryRaw() to the battery_config_t.read_adc callback
 * shape battery_init() expects (see V1's BOARD_ADC_GetBatteryInfo). No
 * current-sense channel on V3, so *current is always 0. */
static void v3_read_battery_adc(uint16_t *voltage, uint16_t *current);

int main(void)
{
    /* CRITICAL (A1.5/A5.3): SystemCoreClock is compiled to a nominal 8 MHz,
     * but the PY32F071 actually boots its HSI at 48 MHz (confirmed on hardware
     * 2026-08-22 via a boot readout) and SystemInit never reconciles the two.
     * Every busy delay and the SysTick reload derive from SystemCoreClock, so
     * the uncorrected 6x error made DelayUs(10) ~1.7 us (keyboard debounce
     * never settled -> missed key presses) and ran the render cadence 6x fast.
     * Recompute SystemCoreClock from the RCC registers BEFORE SCHEDULER_Init
     * (which sets the SysTick reload) and before any DelayMs/DelayUs. */
    SystemCoreClockUpdate();

    /* One-time driver init that doesn't depend on boot ordering below. */
    SCHEDULER_Init();   /* SysTick 10 ms tick — Task 1 (scout_tick_10ms) */
    ADC_Init();         /* battery-voltage ADC front end */

    SPI_LCD_Init();

    /* Bring up the BK4819 chip itself (register defaults / power-up),
     * before radio_init() programs it for a specific frequency/mode. */
    BK4819_Init();

    /* LCD power-up settle + double init (A1.1).
     *
     * Symptom this fixes: booting WITHOUT the USB programming cable attached
     * left the screen blank, while booting with it attached worked — and once
     * running, unplugging the cable kept the display alive. So the panel was
     * only failing to come out of power-on, not losing power later: our
     * ST7565_Init() ran before the LCD's internal supply/charge-pump had
     * settled, and this board drives no /RES line (A1.1) so a botched init is
     * never retried.
     *
     * armel calls ST7565_Init() late in its board init (after backlight, ADC
     * and the SPI-flash bring-up) — i.e. with much more elapsed time since
     * power-on. We now do the same by initialising the panel here rather than
     * first, plus an explicit settle delay, and then run the (idempotent)
     * init a second time so a first attempt made too early is corrected.
     *
     * Timing note: this fix was verified while DelayUs() was a busy-loop that
     * ran ~4.8x too long, so the first init really happened ~0.5 s after
     * boot. DelayUs() now counts SysTick cycles; the 400 ms settle keeps the
     * first init at roughly that proven point (boot to welcome: ~0.75 s,
     * measured). */
    DelayMs(400);
    ST7565_Init();
    DelayMs(20);
    ST7565_Init();

    UI_DisplayWelcome();

    /* Backlight only once the welcome frame is on the panel (as armel does),
     * so the user never sees a lit but blank screen while the LCD settles. */
    BACKLIGHT_On();

    static const radio_config_t radio_cfg = {
        .initial_freq_hz    = SCOUT_BOOT_FREQ_HZ,   /* power-on defaults shared with V1 */
        .initial_modulation = SCOUT_BOOT_MODULATION,
        .initial_bandwidth  = SCOUT_BOOT_BANDWIDTH,
        .initial_squelch    = SCOUT_SQL_DEFAULT,
    };
    radio_init(&radio_cfg);

    /* Battery: the factory ADC calibration word (ADC count at 7.60 V) from
     * the SPI flash, same as V1 reads it from EEPROM (core/calib). If it is
     * blank, use the value measured on the first V3 radio against a
     * multimeter (2026-08-22: raw 2376 at 7.95 V -> 2376 * 760 / 795 = 2271).
     *
     * ADC_GetBatteryRaw() is a single blocking 12-bit read with no
     * current-sense channel on this board, so *current is always 0. */
    const uint16_t adc_760 = calib_battery_adc760();
    const battery_config_t battery_cfg = {
        .adc_760  = adc_760 ? adc_760 : 2271u,
        .read_adc = v3_read_battery_adc,
    };
    battery_init(&battery_cfg);

    static const audio_df_config_t df_cfg = {
        .rssi_floor = 40,   /* match bargraph noise-floor calibration */
        .rssi_ceil  = 200,
        .tone_gain  = 96,
    };
    audio_df_init(&df_cfg);

    /* Keep the welcome screen up for ~1.5 s, then hand off to the app —
     * identical to V1's Main(). */
    DelayMs(1500);

    scout_app_run();

    /* scout_app_run() never returns. If it somehow does, fail safe rather
     * than falling off the end of main() into undefined territory. */
    for (;;)
    {
        /* idle */
    }
}

static void v3_read_battery_adc(uint16_t *voltage, uint16_t *current)
{
    *voltage = ADC_GetBatteryRaw();
    *current = 0;
}
