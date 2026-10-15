/* The remaining HAL pieces: keyboard, PTT, speaker path, TX hardware,
 * factory calibration and the battery ADC. */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "calib/calib.h"
#include "emu/emu.h"
#include "peripherals/audio_path.h"
#include "peripherals/backlight_hal.h"
#include "peripherals/keyboard.h"
#include "radio/tx_hw.h"

/* Declared extern in the shared keyboard.h; the platform defines them. */
KEY_Code_t gKeyReading0 = KEY_INVALID;
KEY_Code_t gKeyReading1 = KEY_INVALID;
uint16_t   gDebounceCounter;
bool       gWasFKeyPressed;

static bool s_audio_on;

/* `calib blank`: the squelch tables (block offsets 0x000..0x0BF) read as
 * erased flash, like a radio whose calibration was never written. */
bool g_emu_calib_blank_squelch;
#define SQUELCH_TABLES_END 0x0C0u

KEY_Code_t KEYBOARD_Poll(void)
{
    return emu_key_state();
}

void AUDIO_AudioPathOn(void)  { s_audio_on = true; }
void AUDIO_AudioPathOff(void) { s_audio_on = false; }
bool emu_audio_path_on(void)  { return s_audio_on; }

/* The real platforms switch the backlight on at boot, before the app. */
static bool s_backlight_on = true;
void backlight_hal_set(bool on) { s_backlight_on = on; }
bool emu_backlight_on(void)     { return s_backlight_on; }

bool TX_HW_PttPressed(void)
{
    return emu_ptt_state();
}

uint8_t TX_HW_PaBias(uint32_t freq_10hz)
{
    (void)freq_10hz;
    return 10;
}

uint8_t TX_HW_MicGain(void)
{
    return 32;
}

void CAL_HW_Read(uint16_t offset, void *buf, uint8_t len)
{
    if ((uint32_t)offset + len > sizeof(g_emu_calib))
    {
        memset(buf, 0xFF, len);   /* reads past the block look blank */
        return;
    }
    memcpy(buf, g_emu_calib + offset, len);
    if (g_emu_calib_blank_squelch)
        for (uint8_t i = 0; i < len; i++)
            if (offset + i < SQUELCH_TABLES_END)
                ((uint8_t *)buf)[i] = 0xFF;
}

/* ~7.98 V with this radio's 7.60 V calibration word (1896). */
void emu_read_adc(uint16_t *voltage, uint16_t *current)
{
    *voltage = 1990;
    *current = 0;
}

/* printf's character sink; the firmware has no console. */
void _putchar(char c)
{
    (void)c;
}
