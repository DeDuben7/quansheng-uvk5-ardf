/* audio_df: continuous tone whose pitch tracks RSSI.
 *
 * Uses BK4819 AF mode ALAM ("Tone Out for Rx") so the receiver stays
 * fully active — RSSI keeps updating while the tone plays. No fight
 * with the RX state machine.
 *
 * The application:
 *   - calls audio_df_init() once at boot
 *   - calls audio_df_toggle() on a button press
 *   - calls audio_df_task_10ms() from the scheduler with the latest RSSI
 *     and a smoothed running-average RSSI (the tone tracks the average)
 */

#ifndef AUDIO_DF_H
#define AUDIO_DF_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    AUDIO_DF_OK = 0,
    AUDIO_DF_ERROR
} audio_df_status_t;

typedef struct {
    // RSSI value at and below which the tone holds the LOWEST pitch.
    uint16_t rssi_floor;
    // RSSI value at and above which the tone holds the HIGHEST pitch.
    uint16_t rssi_ceil;
    // Tone gain register value, 0..127. 96 is full / loud, 28 is the
    // standard quiet beep level.
    uint8_t  tone_gain;
} audio_df_config_t;

audio_df_status_t audio_df_init(const audio_df_config_t* cfg);

// Flip on/off. When turning on, immediately starts the tone.
void audio_df_toggle(void);

void audio_df_enable(void);
void audio_df_disable(void);

bool audio_df_is_active(void);

// Call from the scheduler with the latest smoothed RSSI. Cheap if the
// module is inactive (early return).
void audio_df_task(uint16_t smoothed_rssi);

#endif
