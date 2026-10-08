/* Host emulator internals: the glue between the fake hardware modules,
 * the simulated clock and the scene script. Core code never includes this. */
#ifndef EMU_EMU_H
#define EMU_EMU_H

#include <stdbool.h>
#include <stdint.h>

#include "peripherals/keyboard.h"

/* Simulated clock (emu_clock.c). Time only moves when the firmware waits. */
uint32_t   emu_now_ms(void);
void       emu_advance_ms(uint32_t ms);   /* 1 ms steps, pumping the script */

/* Scene script (emu_script.c). */
int        emu_script_load(const char *scene_line, const char *outdir);
void       emu_script_pump(void);         /* runs due actions; exits when done */
KEY_Code_t emu_key_state(void);
bool       emu_ptt_state(void);

/* ST7565 model (emu_lcd.c) + PNG output (emu_png.c). */
bool       emu_lcd_pixel(int x, int y);   /* x 0..127, y 0..63 */
int        emu_png_write(const char *path);   /* 0 = ok */

/* BK4819 register file (emu_bk4819.c). */
void       emu_bk_set_rssi(uint16_t rssi);              /* everywhere else */
void       emu_bk_add_station(uint32_t hz, uint16_t rssi); /* a carrier on hz */
void       emu_bk_raise_irq(uint16_t reg02_flags);
void       emu_bk_set_carrier(bool on);                 /* sql open|close */
void       emu_bk_tick(void);                           /* every simulated ms */
unsigned   emu_bk_rx_restarts(void);                    /* BK4819_RX_TurnOn calls */
void       emu_bk_reset_counts(void);
uint16_t   emu_bk_reg(uint8_t reg);

/* Other fakes (emu_hal.c). */
bool       emu_audio_path_on(void);
extern bool g_emu_calib_blank_squelch;   /* `calib blank`: squelch tables read 0xFF */
void       emu_read_adc(uint16_t *voltage, uint16_t *current);

/* Factory calibration block of a real V1 (emu_calib_data.c). */
extern const uint8_t g_emu_calib[512];

#endif
