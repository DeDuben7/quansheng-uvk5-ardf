/* scout-v2 entry point.
 * Initialises hardware, shows the AMG welcome briefly, then hands off
 * to the scout application loop.
 */

#include <stdint.h>
#include <string.h>

#include "bsp/dp32g030/syscon.h"

#include "peripherals/backlight.h"
#include "bk4819/bk4819.h"
#include "peripherals/system.h"
#include "peripherals/systick.h"


#include "board.h"
#include "calib/calib.h"
#include "ui/welcome.h"
#include "radio/radio.h"
#include "audio_df/audio_df.h"
#include "battery/battery.h"
#include "scout/app.h"
#include "scout/config.h"

void _putchar(__attribute__((unused)) char c)
{
    // No UART output in scout-v2 yet.
}

void Main(void)
{
    // Enable clock gating for the peripherals we use.
    SYSCON_DEV_CLK_GATE = 0
        | SYSCON_DEV_CLK_GATE_GPIOA_BITS_ENABLE
        | SYSCON_DEV_CLK_GATE_GPIOB_BITS_ENABLE
        | SYSCON_DEV_CLK_GATE_GPIOC_BITS_ENABLE
        | SYSCON_DEV_CLK_GATE_SPI0_BITS_ENABLE
        | SYSCON_DEV_CLK_GATE_SARADC_BITS_ENABLE
        | SYSCON_DEV_CLK_GATE_PWM_PLUS0_BITS_ENABLE;

    SYSTICK_Init();
    BOARD_Init();
    BK4819_Init();

    static const backlight_config_t bl_cfg = {
        .timeout_500ms  = 0,   // never: core scout/light.c switches it
        .brightness_max = 10,
        .brightness_min = 0,
    };
    backlight_init(&bl_cfg);
    backlight_turn_on();

    UI_DisplayWelcome();

    static const radio_config_t radio_cfg = {
        .initial_freq_hz     = SCOUT_BOOT_FREQ_HZ,
        .initial_modulation  = SCOUT_BOOT_MODULATION,
        .initial_bandwidth   = SCOUT_BOOT_BANDWIDTH,
        .initial_squelch     = SCOUT_SQL_DEFAULT,
    };
    radio_init(&radio_cfg);

    // Battery: factory calibration word = ADC count at 7.60 V (1896 on this
    // radio), read via core/calib. battery_init falls back to a nominal value
    // if it is blank (0).
    const battery_config_t bat_cfg = {
        .adc_760  = calib_battery_adc760(),
        .read_adc = BOARD_ADC_GetBatteryInfo,
    };
    battery_init(&bat_cfg);

    static const audio_df_config_t df_cfg = {
        .rssi_floor = 40,   // match bargraph noise-floor calibration
        .rssi_ceil  = 200,
        .tone_gain  = 96,
    };
    audio_df_init(&df_cfg);

    // Keep the welcome on-screen for ~1.5 s, then run the app forever.
    SYSTEM_DelayMs(1500);
    scout_app_run();
}
