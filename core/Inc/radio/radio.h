/* radio: thin wrapper over bk4819 for scout-v2. Owns the current frequency,
 * modulation and bandwidth. Encapsulates the receive-only state machine.
 *
 * Receive by default. A bench-test transmit path (radio_tx_*) exists for
 * testing reception between two radios; the app gates it behind a PIN.
 */

#ifndef RADIO_H
#define RADIO_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    RADIO_OK = 0,
    RADIO_ERROR
} radio_status_t;

typedef enum {
    RADIO_MOD_FM = 0,
    RADIO_MOD_AM = 1
} radio_modulation_t;

typedef enum {
    RADIO_BW_WIDE         = 0,  // ~25 kHz
    RADIO_BW_NARROW       = 1,  // ~12.5 kHz
    RADIO_BW_NARROW_1K7   = 2   // ~1.7 kHz (DF use)
} radio_bandwidth_t;

typedef struct {
    uint32_t initial_freq_hz;
    radio_modulation_t initial_modulation;
    radio_bandwidth_t  initial_bandwidth;
    uint8_t            initial_squelch;    // 0 = open .. RADIO_SQL_MAX
} radio_config_t;

radio_status_t radio_init(const radio_config_t* cfg);

void               radio_set_frequency(uint32_t hz);
uint32_t           radio_get_frequency(void);

// Fast retune for scanning: programs the PLL (+ VCO recalibration) but
// does NOT update the stored frequency, clear mistune, or re-pick the
// RX filter path (pick it once via radio_set_frequency before a sweep).
// Always restore with radio_set_frequency() when the sweep ends.
void radio_tune_raw(uint32_t hz);

void               radio_set_modulation(radio_modulation_t mod);
radio_modulation_t radio_get_modulation(void);

void               radio_set_bandwidth(radio_bandwidth_t bw);
radio_bandwidth_t  radio_get_bandwidth(void);

// Manual front-end gain (REG_13). AGC is disabled while a manual index
// is active. Index range: 0 (most attenuated, -79 dB) to RADIO_GAIN_MAX
// (full gain, 0 dB). Ported from reald's ARDF gain table (app/ardf.c).
#define RADIO_GAIN_MAX     16
#define RADIO_GAIN_DEFAULT 7   // reald's ARDF_GAIN_INDEX_DEFAULT

void    radio_set_gain_index(uint8_t idx);
// Calibration only: write a raw REG_13 front-end gain value. Any later
// radio_set_gain_index() / gain step restores the table value.
void    radio_set_raw_gain(uint16_t reg13);
uint8_t radio_get_gain_index(void);

// Step the gain one level up/down. Stepping DOWN past index 0 engages
// "mistune": the RX is deliberately detuned so filter rejection adds
// attenuation beyond what the front-end can do (reald's final-approach
// trick). Stepping UP first exits mistune, then raises gain.
void    radio_gain_incr(void);
void    radio_gain_decr(void);
bool    radio_mistune_active(void);

// Nominal gain of the current step relative to full gain, in dB
// (0 = full gain, -79 = most attenuated). Per-device calibration values;
// see the table comment in radio.c.
int8_t  radio_get_gain_db(void);

// --- Squelch ----------------------------------------------------------------
// Level 0 = open: audio always on (the fox-hunt noise floor). Levels 1..9
// use the radio's factory squelch tables, with the RSSI thresholds lowered
// by the manual front-end attenuation (SCOUT_SQL_GAIN_COMPENSATION). A
// band without a usable factory table behaves as level 0. The audio gate
// follows the BK4819's squelch interrupts.
#define RADIO_SQL_MAX 9

void    radio_set_squelch(uint8_t level);
uint8_t radio_get_squelch(void);
// Call every main-loop pass: opens/mutes the audio on squelch interrupts.
// Not while something else drives the receiver (scanner sweep, gain sweep):
// call radio_squelch_resync() when that is over.
void    radio_squelch_task(void);
void    radio_squelch_resync(void);
// true = audio passes (signal present, level 0, or no factory table).
bool    radio_squelch_is_open(void);
// true while another module (audio_df) owns the speaker: the gate keeps
// tracking the squelch but leaves the audio alone. false re-applies it.
void    radio_audio_hold(bool hold);

// Raw RSSI register value (0..255 typical, higher = stronger signal).
uint16_t radio_get_rssi(void);

// Converted dBm value. No band correction applied here — pass it in if
// you want one, or feed via your own LUT.
int16_t  radio_get_rssi_dbm(int8_t band_correction_db);

// --- Transmit (bench-test) -----------------------------------------------
// FM voice from the microphone on the current frequency at a very low power
// level. Only on the amateur 2 m (144-146 MHz) and 70 cm (430-440 MHz) bands.
bool           radio_tx_allowed(uint32_t hz);
radio_status_t radio_tx_start(void);   // RADIO_ERROR if not allowed/already on
void           radio_tx_stop(void);    // back to RX, full RX config restored
bool           radio_is_transmitting(void);

#endif
