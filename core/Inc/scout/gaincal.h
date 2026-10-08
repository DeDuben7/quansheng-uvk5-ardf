/* Gain sweep: measure the averaged RSSI of a steady carrier (or the noise
 * floor, carrier off) at a list of front-end gain settings (REG_13), for
 * building a per-radio gain table. Opened from the info screen (MENU).
 */
#ifndef SCOUT_GAINCAL_H
#define SCOUT_GAINCAL_H

#include <stdbool.h>
#include "peripherals/keyboard.h"

void gaincal_enter(void);                    // starts a sweep
void gaincal_task(void);                     // call every main-loop pass
bool gaincal_handle_key(KEY_Code_t key);     // false = leave the screen
void gaincal_render(void);

#endif
