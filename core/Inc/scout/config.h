/* Power-on defaults. Nothing is stored: every power-on starts here. Shared
 * by all platforms and the host emulator, so V1 and V3 can't drift apart. */
#ifndef SCOUT_CONFIG_H
#define SCOUT_CONFIG_H

#include "radio/radio.h"

#define SCOUT_BOOT_FREQ_HZ      144450000u      /* 144.450 MHz */
#define SCOUT_BOOT_MODULATION   RADIO_MOD_FM
#define SCOUT_BOOT_BANDWIDTH    RADIO_BW_NARROW
#define SCOUT_70CM_PRESET_HZ    433500000u      /* first 70 cm frequency after power-on */

#define SCOUT_SQL_DEFAULT       3u              /* 0 = open, 1..9 */

/* 1: lower the squelch RSSI thresholds by the manual front-end attenuation
 * (radio.c squelch_apply), so squelch sensitivity doesn't depend on the
 * gain setting. 0: factory thresholds as-is (reald behaviour). */
#define SCOUT_SQL_GAIN_COMPENSATION 1

#endif
