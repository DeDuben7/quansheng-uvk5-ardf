/* backlight: pure peripheral module owning its own state.
 *
 * The application provides a backlight_config_t at init time, then drives
 * the module via the snake_case API. No globals leak in or out — the
 * scheduler calls backlight_task_500ms() periodically to handle the
 * auto-off timeout.
 */

#ifndef PERIPHERALS_BACKLIGHT_H
#define PERIPHERALS_BACKLIGHT_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BACKLIGHT_OK = 0,
    BACKLIGHT_ERROR
} backlight_status_t;

typedef struct {
    // Auto-off timeout in 500 ms units. 0 = backlight stays on indefinitely
    // once turned on. Examples: 10 = 5 s, 120 = 1 min.
    uint16_t timeout_500ms;
    // PWM brightness index used when the backlight is fully on. The
    // hardware produces a duty cycle of (1 << brightness_max) - 1.
    // Typical range 1..10.
    uint8_t brightness_max;
    // Brightness index used when the backlight is logically "off". 0 means
    // genuinely dark; >0 leaves a dim glow.
    uint8_t brightness_min;
} backlight_config_t;

backlight_status_t backlight_init(const backlight_config_t* cfg);

void backlight_turn_on(void);
void backlight_turn_off(void);
bool backlight_is_on(void);

void backlight_set_brightness(uint8_t brightness);
uint8_t backlight_get_brightness(void);

// Call once every 500 ms (e.g. from the scheduler). Decrements the auto-off
// countdown and turns the backlight off when it reaches zero. Returns true
// if a timeout-induced turn-off fired this tick.
bool backlight_task_500ms(void);

#endif
