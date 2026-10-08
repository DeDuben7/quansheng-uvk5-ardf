/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

/* Shared ST7565 LCD driver (core). Talks to the panel exclusively through the
 * small per-platform LCD HAL declared in "peripherals/spi_lcd.h"
 * (SPI_LCD_Init/WriteByte, LCD_CS_Low/High, LCD_A0_Command/Data) plus
 * SYSTEM_DelayMs() from "peripherals/system.h". Every platform provides both
 * headers, so this file compiles unchanged against any of them.
 *
 * Includes a dirty-page shadow buffer: the ST7565 blit was a full 7-page
 * (896-byte) synchronous SPI transfer every ~10 Hz render, which blocks the
 * CPU (and starves the keyboard poll). Keep a shadow of what was last pushed
 * to each page and only re-transmit pages whose contents actually changed. On
 * the home screen typically only the RSSI bar + a couple of digits change per
 * frame, so most frames send 1-2 pages instead of 7. Costs ~1 KB of RAM.
 * ST7565_Init/FillScreen touch the panel outside the shadow's knowledge, so
 * they invalidate it to force the next blit to fully repaint.
 */

#include <stdint.h>
#include <stdio.h>     // NULL
#include <string.h>    // memcmp / memcpy (dirty-page diffing)

#include "common.h"
#include "peripherals/spi_lcd.h"
#include "peripherals/system.h"
#include "st7565/st7565.h"

/* --- Dirty-page diffing shadow state (see file header) --- */
static uint8_t s_frame_shadow[FRAME_LINES][LCD_WIDTH];
static uint8_t s_status_shadow[LCD_WIDTH];
static bool    s_frame_shadow_valid;
static bool    s_status_shadow_valid;

uint8_t gStatusLine[LCD_WIDTH];
uint8_t gFrameBuffer[FRAME_LINES][LCD_WIDTH];

static void DrawLine(uint8_t column, uint8_t line, const uint8_t * lineBuffer, unsigned size_defVal)
{
	ST7565_SelectColumnAndLine(column + 4, line);
	LCD_A0_Data();
	for (unsigned i = 0; i < size_defVal; i++) {
		SPI_LCD_WriteByte(lineBuffer ? lineBuffer[i] : size_defVal);
	}
}

void ST7565_DrawLine(const unsigned int Column, const unsigned int Line, const uint8_t *pBitmap, const unsigned int Size)
{
	LCD_CS_Low();
	DrawLine(Column, Line, pBitmap, Size);
	LCD_CS_High();
	/* Panel written directly, bypassing the shadow — invalidate it so the
	 * next full blit doesn't skip this page as "unchanged". */
	s_frame_shadow_valid = false;
}

void ST7565_BlitFullScreen(void)
{
	LCD_CS_Low();
	ST7565_WriteByte(0x40);
	for (unsigned line = 0; line < FRAME_LINES; line++) {
		/* Skip pages whose contents are unchanged since the last blit. */
		if (s_frame_shadow_valid &&
		    memcmp(gFrameBuffer[line], s_frame_shadow[line], LCD_WIDTH) == 0) {
			continue;
		}
		DrawLine(0, line+1, gFrameBuffer[line], LCD_WIDTH);
		memcpy(s_frame_shadow[line], gFrameBuffer[line], LCD_WIDTH);
	}
	s_frame_shadow_valid = true;
	LCD_CS_High();
}

void ST7565_BlitLine(unsigned line)
{
	if (line >= FRAME_LINES) {
		return;
	}
	LCD_CS_Low();
	ST7565_WriteByte(0x40);    // start line ?
	DrawLine(0, line+1, gFrameBuffer[line], LCD_WIDTH);
	memcpy(s_frame_shadow[line], gFrameBuffer[line], LCD_WIDTH);  /* keep the shadow consistent */
	LCD_CS_High();
}

void ST7565_BlitStatusLine(void)
{	// the top small text line on the display
	LCD_CS_Low();
	/* Only re-transmit the status page when it actually changed. */
	if (!(s_status_shadow_valid &&
	      memcmp(gStatusLine, s_status_shadow, LCD_WIDTH) == 0)) {
		ST7565_WriteByte(0x40);    // start line ?
		DrawLine(0, 0, gStatusLine, LCD_WIDTH);
		memcpy(s_status_shadow, gStatusLine, LCD_WIDTH);
		s_status_shadow_valid = true;
	}
	LCD_CS_High();
}

void ST7565_FillScreen(uint8_t value)
{
	LCD_CS_Low();
	for (unsigned i = 0; i < 8; i++) {
		DrawLine(0, i, NULL, value);
	}
	LCD_CS_High();
	/* Panel written directly, bypassing the shadow — force a full repaint. */
	s_frame_shadow_valid  = false;
	s_status_shadow_valid = false;
}

// Software reset
const uint8_t ST7565_CMD_SOFTWARE_RESET = 0xE2;
// Bias Select
// 1 0 1 0 0 0 1 BS
// Select bias setting 0=1/9; 1=1/7 (at 1/65 duty)
const uint8_t ST7565_CMD_BIAS_SELECT = 0xA2;
// COM Direction
// 1 1 0 0 MY - - -
// Set output direction of COM
// MY=1, reverse direction
// MY=0, normal direction
const uint8_t ST7565_CMD_COM_DIRECTION = 0xC0;
// SEG Direction
// 1 0 1 0 0 0 0 MX
// Set scan direction of SEG
// MX=1, reverse direction
// MX=0, normal direction
const uint8_t ST7565_CMD_SEG_DIRECTION = 0xA0;
// Inverse Display
// 1 0 1 0 0 1 1 INV
// INV =1, inverse display
// INV =0, normal display
const uint8_t ST7565_CMD_INVERSE_DISPLAY = 0xA6;
// All Pixel ON
// 1 0 1 0 0 1 0 AP
// AP=1, set all pixel ON
// AP=0, normal display
const uint8_t ST7565_CMD_ALL_PIXEL_ON = 0xA4;
// Regulation Ratio
// 0 0 1 0 0 RR2 RR1 RR0
// This instruction controls the regulation ratio of the built-in regulator
const uint8_t ST7565_CMD_REGULATION_RATIO = 0x20;
// Double command!! Set electronic volume (EV) level
// Send next: 0 0 EV5 EV4 EV3 EV2 EV1 EV0  contrast 0-63
const uint8_t ST7565_CMD_SET_EV = 0x81;
// Control built-in power circuit ON/OFF - 0 0 1 0 1 VB VR VF
// VB: Built-in Booster
// VR: Built-in Regulator
// VF: Built-in Follower
const uint8_t ST7565_CMD_POWER_CIRCUIT = 0x28;
// Set display start line 0-63
// 0 0 0 1 S5 S4 S3 S2 S1 S0
const uint8_t ST7565_CMD_SET_START_LINE = 0x40;
// Display ON/OFF
// 0 0 1 0 1 0 1 1 1 D
// D=1, display ON
// D=0, display OFF
const uint8_t ST7565_CMD_DISPLAY_ON_OFF = 0xAE;

uint8_t cmds[] = {
	ST7565_CMD_BIAS_SELECT | 0, 			// Select bias setting: 1/9
	ST7565_CMD_COM_DIRECTION  | (0 << 3), 	// Set output direction of COM: normal
	ST7565_CMD_SEG_DIRECTION | 1, 			// Set scan direction of SEG: reverse
	ST7565_CMD_INVERSE_DISPLAY | 0, 		// Inverse Display: false
	ST7565_CMD_ALL_PIXEL_ON | 0, 			// All Pixel ON: false - normal display
	ST7565_CMD_REGULATION_RATIO | (4 << 0), // Regulation Ratio 5.0

	ST7565_CMD_SET_EV,						// Set contrast
	31,

	ST7565_CMD_POWER_CIRCUIT | 0b111,		// Built-in power circuit ON/OFF: VB=1 VR=1 VF=1
	ST7565_CMD_SET_START_LINE | 0,			// Set Start Line: 0
	ST7565_CMD_DISPLAY_ON_OFF | 1,			// Display ON/OFF: ON
};

int16_t map(int16_t x, int16_t in_min, int16_t in_max, int16_t out_min, int16_t out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}


void ST7565_Init(void)
{
	SPI_LCD_Init();
	ST7565_HardwareReset();
	LCD_CS_Low();
	ST7565_WriteByte(ST7565_CMD_SOFTWARE_RESET);   // software reset
	SYSTEM_DelayMs(120);

	for(uint8_t i = 0; i < 8; i++)
		ST7565_WriteByte(cmds[i]);

	ST7565_WriteByte(ST7565_CMD_POWER_CIRCUIT | 0b011);   // VB=0 VR=1 VF=1
	SYSTEM_DelayMs(1);
	ST7565_WriteByte(ST7565_CMD_POWER_CIRCUIT | 0b110);   // VB=1 VR=1 VF=0
	SYSTEM_DelayMs(1);

	for(uint8_t i = 0; i < 4; i++) // why 4 times?
		ST7565_WriteByte(ST7565_CMD_POWER_CIRCUIT | 0b111);   // VB=1 VR=1 VF=1

	SYSTEM_DelayMs(40);

	ST7565_WriteByte(ST7565_CMD_SET_START_LINE | 0);   // line 0
	ST7565_WriteByte(ST7565_CMD_DISPLAY_ON_OFF | 1);   // D=1
	LCD_CS_High();

	ST7565_FillScreen(0x00);
}

void ST7565_FixInterfGlitch(void)
{
	LCD_CS_Low();
	for(uint8_t i = 0; i < ARRAY_SIZE(cmds); i++)
		ST7565_WriteByte(cmds[i]);
	LCD_CS_High();
}

void ST7565_SelectColumnAndLine(uint8_t Column, uint8_t Line)
{
	LCD_A0_Command();
	SPI_LCD_WriteByte(Line + 176);
	SPI_LCD_WriteByte(((Column >> 4) & 0x0F) | 0x10);
	SPI_LCD_WriteByte((Column >> 0) & 0x0F);
}

void ST7565_WriteByte(uint8_t Value)
{
	LCD_A0_Command();
	SPI_LCD_WriteByte(Value);
}
