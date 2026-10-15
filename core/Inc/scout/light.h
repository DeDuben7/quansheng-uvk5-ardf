/* LCD backlight mode (Menu > Verlichting): automatic, always on, always off.
 *
 * Automatic: every key press (and PTT) lights the backlight; it goes off
 * LIGHT_AUTO_OFF_MS after the last one. The key still does its normal job.
 * The radio starts in automatic at every power-on; the mode is not saved.
 */

#ifndef SCOUT_LIGHT_H
#define SCOUT_LIGHT_H

#define LIGHT_AUTO_OFF_MS 20000u

typedef enum {
    LIGHT_AUTO = 0,
    LIGHT_ON,
    LIGHT_OFF,
    LIGHT_MODE_COUNT
} light_mode_t;

// Automatic mode, backlight on, timer started.
void         light_init(void);
void         light_set_mode(light_mode_t mode);
light_mode_t light_get_mode(void);
// A key press or PTT: restarts the automatic timer and lights the backlight.
void         light_activity(void);
// Call every main-loop iteration: turns the backlight off when the
// automatic timer runs out.
void         light_task(void);

#endif
