/* scout TX — see scout/tx.h. */

#include <stddef.h>

#include "scout/tx.h"
#include "radio/radio.h"
#include "radio/tx_hw.h"
#include "audio_df/audio_df.h"
#include "peripherals/scheduler.h"

// Not stored anywhere: TX is locked again at every power-on.
static const uint8_t TX_PIN[SCOUT_TX_PIN_LEN] = { 4, 5, 7, 6 };

#define PTT_DEBOUNCE_SAMPLES 3u      // x ~10 ms loop = ~30 ms
#define TX_TIMEOUT_TICKS     6000u   // 60 s, in 10 ms ticks
#define MSG_TICKS            200u    // status text shown ~2 s

static bool     s_unlocked;
static bool     s_tx;
static uint32_t s_tx_start_tick;
static bool     s_df_was_active;

static bool     s_ptt_stable;        // debounced PTT level
static uint8_t  s_ptt_count;
static bool     s_wait_release;      // ignore PTT until released

static bool     s_pin_active;
static uint8_t  s_pin[SCOUT_TX_PIN_LEN];
static uint8_t  s_pin_len;

static const char *s_msg;
static uint32_t    s_msg_tick;

static void show(const char *msg)
{
    s_msg      = msg;
    s_msg_tick = scout_tick_10ms();
}

static void tx_begin(void)
{
    // The DF tone generator shares the modulator; park it while on air.
    s_df_was_active = audio_df_is_active();
    if (s_df_was_active) audio_df_disable();

    if (radio_tx_start() != RADIO_OK)
    {
        if (s_df_was_active) audio_df_enable();
        show("TX NOT ALLOWED");
        s_wait_release = true;
        return;
    }
    s_tx            = true;
    s_tx_start_tick = scout_tick_10ms();
}

static void tx_end(void)
{
    radio_tx_stop();
    s_tx = false;
    if (s_df_was_active) audio_df_enable();
}

static void ptt_debounce(void)
{
    const bool raw = TX_HW_PttPressed();
    if (raw == s_ptt_stable)
    {
        s_ptt_count = 0;
        return;
    }
    if (++s_ptt_count >= PTT_DEBOUNCE_SAMPLES)
    {
        s_ptt_stable = raw;
        s_ptt_count  = 0;
    }
}

void scout_tx_task(bool can_start)
{
    ptt_debounce();

    if (s_msg != NULL && (scout_tick_10ms() - s_msg_tick) >= MSG_TICKS)
        s_msg = NULL;

    if (!s_ptt_stable)
    {
        s_wait_release = false;
        if (s_tx) tx_end();
        return;
    }

    // PTT held.
    if (s_tx)
    {
        if ((scout_tick_10ms() - s_tx_start_tick) >= TX_TIMEOUT_TICKS)
        {
            tx_end();
            show("TX TIMEOUT");
            s_wait_release = true;
        }
        return;
    }
    if (s_wait_release) return;

    s_wait_release = true;   // one attempt per press, whatever the outcome
    if (!s_unlocked)
        show("TX LOCKED: F+PIN");
    else if (!can_start)
        show("TX NOT HERE");
    else if (!radio_tx_allowed(radio_get_frequency()))
        show("TX: NOT HAM BAND");
    else
    {
        s_wait_release = false;
        tx_begin();
    }
}

bool scout_tx_active(void)   { return s_tx; }
bool scout_tx_unlocked(void) { return s_unlocked; }

uint16_t scout_tx_elapsed_s(void)
{
    return s_tx ? (uint16_t)((scout_tick_10ms() - s_tx_start_tick) / 100u) : 0;
}

void scout_tx_key_f(void)
{
    if (s_unlocked)
    {
        s_unlocked = false;
        show("TX LOCKED");
        return;
    }
    s_pin_active = true;
    s_pin_len    = 0;
}

bool    scout_tx_pin_active(void) { return s_pin_active; }
uint8_t scout_tx_pin_len(void)    { return s_pin_len; }

void scout_tx_pin_digit(uint8_t digit)
{
    if (!s_pin_active || digit > 9) return;
    s_pin[s_pin_len++] = digit;
    if (s_pin_len < SCOUT_TX_PIN_LEN) return;

    bool ok = true;
    for (uint8_t i = 0; i < SCOUT_TX_PIN_LEN; i++)
        if (s_pin[i] != TX_PIN[i]) ok = false;

    s_pin_active = false;
    s_pin_len    = 0;
    s_unlocked   = ok;
    show(ok ? "TX UNLOCKED" : "WRONG PIN");
}

void scout_tx_pin_cancel(void)
{
    s_pin_active = false;
    s_pin_len    = 0;
}

const char *scout_tx_message(void)
{
    return s_msg;
}
