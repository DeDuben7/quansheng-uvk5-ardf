/* Home screen renderer. Shows frequency, modulation, RSSI value + bar,
 * and an audio-DF indicator when active. Stateless except for the
 * input-buffer for in-progress frequency entry.
 */

#ifndef SCOUT_HOME_H
#define SCOUT_HOME_H

#include <stdint.h>
#include <stdbool.h>

void scout_home_render(void);

// Push a digit into the in-progress frequency-entry buffer. Returns true
// if the buffer is now full and the application should call _commit().
bool scout_home_input_digit(uint8_t digit);
void scout_home_input_clear(void);
// Apply the buffered frequency to the radio module. No-op if buffer is
// not at the full 6-digit length.
void scout_home_input_commit(void);
bool scout_home_input_active(void);

#endif
