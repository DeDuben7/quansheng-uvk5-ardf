/* scan: one-shot band sweep that ranks the 5 strongest frequencies.
 *
 * Lifecycle: scan_enter() saves radio state and starts a sweep. While
 * scan_is_sweeping() the app calls scan_task() as often as it can (each
 * call is one settle check or one step, it never blocks), and
 * scan_render() at render cadence while the scanner screen is active.
 * scan_handle_key() consumes
 * EXIT (abort + restore), '*' (rescan) and digits 1-5 (tune to that
 * candidate). When scan_is_done() returns true the app returns to the
 * home screen; the radio is already restored / retuned by then.
 */

#ifndef SCOUT_SCAN_H
#define SCOUT_SCAN_H

#include <stdint.h>
#include <stdbool.h>

#include "peripherals/keyboard.h"

void scan_enter(void);
void scan_task(void);
bool scan_is_sweeping(void);
bool scan_handle_key(KEY_Code_t key, bool is_long);
bool scan_is_done(void);
void scan_render(void);

#endif
