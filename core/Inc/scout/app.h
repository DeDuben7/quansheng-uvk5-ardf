/* scout-v2 application entry. main.c hands off here after hardware init.
 * Never returns.
 */

#ifndef SCOUT_APP_H
#define SCOUT_APP_H

#include <stdint.h>

// Every screen is re-rendered once per frame: 40 ms, 25 Hz.
#define SCOUT_FRAME_MS 40u

void     scout_app_run(void);
// Latest raw RSSI reading (no averaging).
uint16_t scout_app_get_rssi_raw(void);
// 300 ms moving-average RSSI; updated every 10 ms by the main loop.
uint16_t scout_app_get_rssi_avg(void);
// RSSI readings in the last full second, and how many of them differed
// from the reading before (how often the chip updates its RSSI).
void     scout_app_get_rssi_rate(uint16_t* reads_per_s, uint16_t* changes_per_s);

#endif
