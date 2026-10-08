/* scout-v2 application entry. main.c hands off here after hardware init.
 * Never returns.
 */

#ifndef SCOUT_APP_H
#define SCOUT_APP_H

#include <stdint.h>

void     scout_app_run(void);
// 100 ms moving-average RSSI; updated every 10 ms by the main loop.
uint16_t scout_app_get_rssi_avg(void);

#endif
