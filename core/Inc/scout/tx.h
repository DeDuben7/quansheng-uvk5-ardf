/* scout TX: PIN-gated bench-test transmit.
 *
 * TX is locked at every boot (nothing is stored). F opens a PIN prompt; the
 * correct PIN unlocks TX until power-off (F again re-locks). While unlocked,
 * holding PTT transmits the microphone on the current frequency (2 m / 70 cm
 * amateur bands only, very low power, 60 s timeout).
 */

#ifndef SCOUT_TX_H
#define SCOUT_TX_H

#include <stdbool.h>
#include <stdint.h>

#define SCOUT_TX_PIN_LEN 4

// Call every main-loop iteration. can_start = PTT may begin a transmission
// now (home screen, no frequency entry in progress).
void scout_tx_task(bool can_start);

bool     scout_tx_active(void);       // transmitting right now
bool     scout_tx_unlocked(void);
bool     scout_tx_ptt_held(void);     // PTT pressed (debounced), locked or not
uint16_t scout_tx_elapsed_s(void);    // seconds into the current TX

// F key: locked -> open PIN prompt, unlocked -> lock again.
void scout_tx_key_f(void);

// PIN prompt.
bool    scout_tx_pin_active(void);
uint8_t scout_tx_pin_len(void);       // digits typed so far
void    scout_tx_pin_digit(uint8_t digit);
void    scout_tx_pin_cancel(void);

// Short-lived status text for the home screen, or NULL.
const char *scout_tx_message(void);

#endif
