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

/* V3 (PY32F071) platform layer for the shared BK4819 driver
 * (core/Src/bk4819/bk4819.c). Bit-bangs the chip's 3-wire bus on PY32
 * GPIOF9 (CS/SCN), GPIOB8 (SCL), GPIOB9 (SDA) and carries this board's chip
 * bring-up register tables (BK4819_Init / BK4819_RX_TurnOn), which use a
 * different BK4819 chip variant than V1's and are NOT interchangeable with
 * V1's tables (using V1's values on this chip made RSSI read exactly 0).
 * Moved verbatim out of this platform's old Src/bk4819/bk4819.c (Phase C
 * bk4819.c split) — no register values here changed.
 *
 * V3 (PY32F071) port notes — Phase 2 Task 1 (preserved from the original
 * bk4819.c this file was split out of):
 *
 * This is V1's Src/bk4819/bk4819.c (DualTachyon driver) with ONLY the MCU
 * comms surface rewired onto the PY32 GPIO bus. Every register-level
 * sequence, tune helper, RSSI/AF/GpioOut/AGC/gain code in
 * core/Src/bk4819/bk4819.c is byte-for-byte unchanged from V1 — gain-table
 * values are per-radio and uncalibrated for V3 by design (assumption A2.6),
 * and are NOT touched here.
 *
 * What changed, and why:
 *
 *  - Bit-bang pins: V1 drives DP32 GPIOC0/1/2 (SCN/SCL/SDA) through
 *    GPIO_SetBit/ClearBit on &GPIOC->DATA plus PORTCON_PORTC_IE for the SDA
 *    input-enable flip. V3 has no DP32 GPIO/PORTCON block, so the pins move
 *    to PY32 GPIOF9 (CS/SCN), GPIOB8 (SCL), GPIOB9 (SDA) — the exact pins
 *    armel's working V3 firmware uses (authoritative reference:
 *    armel-k5v3-src/App/driver/bk4829.c:31-33,
 *        #define PIN_CSN GPIO_MAKE_PIN(GPIOF, LL_GPIO_PIN_9)
 *        #define PIN_SCL GPIO_MAKE_PIN(GPIOB, LL_GPIO_PIN_8)
 *        #define PIN_SDA GPIO_MAKE_PIN(GPIOB, LL_GPIO_PIN_9)
 *    ). The CS_Assert/CS_Release/SCL_Set/SCL_Reset/SDA_Set/SDA_Reset/
 *    SDA_SetDir/SDA_ReadInput helpers below are transcribed from armel
 *    bk4829.c:54-92, but reimplemented against raw CMSIS registers
 *    (MODER/BSRR/IDR) instead of armel's LL_GPIO_* calls, to match this
 *    port's no-HAL, bare-register style already used in
 *    Src/peripherals/spi_lcd.c and main.c. CS is active-low (idle high),
 *    same polarity as V1 (GPIOC_PIN_BK4819_SCN idles high, is cleared
 *    around each transfer) and armel (CS_Release = SetOutputPin,
 *    CS_Assert = ResetOutputPin).
 *
 *  - Bit-bang timing: V1 calls SYSTICK_DelayUs(1) between every bit-bang
 *    edge. V3 has no SysTick-backed microsecond delay wired up for this
 *    (Phase 1's DelayUs() is a calibrated busy-loop, not cycle-exact — see
 *    Inc/peripherals/delay.h) and, more importantly, this bus needs to run
 *    much faster than a 1us-granularity call can give it. Per the task
 *    brief, timing is instead a NOP-based SHORT_DELAY() macro transcribed
 *    verbatim from armel bk4829.c:42-50 (8 lines of 5 "nop" each = 40 NOPs).
 *
 *  - BK4819_ReadU16's SDA direction flip: V1 flips DP32 PORTCON_PORTC_IE
 *    (input-enable) + GPIOC->DIR to read SDA, then flips back. V3 has no
 *    PORTCON block; the PY32 equivalent is flipping GPIOB->MODER bit-field
 *    for pin 9 between 00 (input) and 01 (output-push-pull) — SDA_SetDir()
 *    below, transcribed from armel bk4829.c:84-87
 *    (`LL_GPIO_SetPinMode(..., Output ? LL_GPIO_MODE_OUTPUT : LL_GPIO_MODE_INPUT)`)
 *    reimplemented against MODER directly. BK4819_ReadU16 itself
 *    (armel bk4829.c:204-224) is transcribed 1:1 onto the new helpers.
 *
 *  - BK4819_Init's idle-line setup (CS high, SCL/SDA high) is kept, moved
 *    onto the new pins via the same helpers. A new static
 *    bk4819_bus_gpio_init() (not present in V1, added per the task brief)
 *    enables the GPIOF+GPIOB clocks and configures CS/SCL/SDA as
 *    push-pull outputs before the idle-line writes — V1 relied on DP32
 *    board/system init elsewhere in the codebase to have already done this
 *    for GPIOC; V3 has no such board-init step yet (same situation
 *    Src/peripherals/spi_lcd.c was in for the LCD SPI/GPIO pins), so this
 *    driver now does its own GPIO bring-up.
 *
 *  - The chip bring-up tables (BK4819_Init / BK4819_RX_TurnOn) below use
 *    armel-derived register values for this board's BK4819 variant —
 *    see e.g. REG_37=0x9D1F in Init, REG_37=0x9F1F / REG_30=0xBFF1 in
 *    RX_TurnOn, and the AGC block REG_10..REG_14. Using V1's chip tables
 *    on this hardware made RSSI read exactly 0 (no reception at all).
 */

#include <stdbool.h>
#include <stdint.h>

#include "bk4819/bk4819.h"
#include "bk4819/bk4819_hw.h"
#include "py32f0xx.h"

#ifndef ARRAY_SIZE
	#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

/* --- PY32 bit-bang bus: pins, helpers, timing ---
 *
 * Pins per armel-k5v3-src/App/driver/bk4829.c:31-33 (authoritative V3
 * reference — CS=GPIOF9, SCL=GPIOB8, SDA=GPIOB9). V1 used DP32
 * GPIOC0/1/2 (SCN/SCL/SDA); those DP32 GPIO/PORTCON headers are gone.
 */
#define BK4819_CS_PIN  9u  /* PF9  (SCN / CS, active-low) */
#define BK4819_SCL_PIN 8u  /* PB8  (SCL)                  */
#define BK4819_SDA_PIN 9u  /* PB9  (SDA)                  */

/* NOP-based bit-bang delay, transcribed verbatim from armel
 * bk4829.c:42-50 (8 lines x 5 "nop" = 40 NOPs). Replaces V1's
 * SYSTICK_DelayUs(1) between bit-bang edges. */
#define SHORT_DELAY()                                                          \
  __asm volatile("nop\n nop\n nop\n nop\n nop\n"                               \
                 "nop\n nop\n nop\n nop\n nop\n"                               \
                 "nop\n nop\n nop\n nop\n nop\n"                               \
                 "nop\n nop\n nop\n nop\n nop\n"                               \
                 "nop\n nop\n nop\n nop\n nop\n"                               \
                 "nop\n nop\n nop\n nop\n nop\n"                               \
                 "nop\n nop\n nop\n nop\n nop\n"                               \
                 "nop\n nop\n nop\n nop\n nop\n")

/* One-time GPIO bring-up for the bit-bang bus: enable the GPIOF/GPIOB
 * clocks and configure CS (PF9) and SCL (PB8) as push-pull outputs, and
 * SDA (PB9) as a push-pull output by default (BK4819_ReadU16 flips it to
 * input for the duration of a read, see SDA_SetDir() below). Not present
 * in V1 (DP32 board/system init did this for GPIOC elsewhere); added here
 * per the task brief since V3 has no such board-init step yet. */
static void bk4819_bus_gpio_init(void)
{
	RCC->IOPENR |= RCC_IOPENR_GPIOBEN | RCC_IOPENR_GPIOFEN;
	(void)RCC->IOPENR; /* read-back: ensure the enable took effect */

	/* PF9 (CS): general-purpose output, push-pull. */
	GPIOF->MODER &= ~(3u << (BK4819_CS_PIN * 2u));
	GPIOF->MODER |=  (1u << (BK4819_CS_PIN * 2u));
	GPIOF->OTYPER &= ~(1u << BK4819_CS_PIN);

	/* PB8 (SCL) / PB9 (SDA): general-purpose output, push-pull. */
	GPIOB->MODER &= ~((3u << (BK4819_SCL_PIN * 2u)) | (3u << (BK4819_SDA_PIN * 2u)));
	GPIOB->MODER |=  ((1u << (BK4819_SCL_PIN * 2u)) | (1u << (BK4819_SDA_PIN * 2u)));
	GPIOB->OTYPER &= ~((1u << BK4819_SCL_PIN) | (1u << BK4819_SDA_PIN));
}

/* CS/SCL/SDA drive + SDA direction-flip/read, transcribed from armel
 * bk4829.c:54-92 (CS_Assert/CS_Release/SCL_Reset/SCL_Set/SDA_Reset/
 * SDA_Set/SDA_SetDir/SDA_ReadInput), reimplemented against raw CMSIS
 * registers instead of armel's LL_GPIO_* wrappers. CS is active-low
 * (armel: CS_Assert = GPIO_ResetOutputPin, CS_Release = SetOutputPin) —
 * same polarity as V1's GPIOC_PIN_BK4819_SCN. */
static inline void CS_Assert(void)
{
	GPIOF->BSRR = (1u << (BK4819_CS_PIN + 16u));
}

static inline void CS_Release(void)
{
	GPIOF->BSRR = (1u << BK4819_CS_PIN);
}

static inline void SCL_Reset(void)
{
	GPIOB->BSRR = (1u << (BK4819_SCL_PIN + 16u));
}

static inline void SCL_Set(void)
{
	GPIOB->BSRR = (1u << BK4819_SCL_PIN);
}

static inline void SDA_Reset(void)
{
	GPIOB->BSRR = (1u << (BK4819_SDA_PIN + 16u));
}

static inline void SDA_Set(void)
{
	GPIOB->BSRR = (1u << BK4819_SDA_PIN);
}

/* PB9 direction flip for BK4819_ReadU16: MODER field 00 = input,
 * 01 = general-purpose output push-pull. Transcribed from armel
 * bk4829.c:84-87 (LL_GPIO_SetPinMode(..., Output ? LL_GPIO_MODE_OUTPUT
 * : LL_GPIO_MODE_INPUT)), reimplemented against MODER directly — this
 * replaces V1's PORTCON_PORTC_IE + GPIOC->DIR flip (DP32 has no MODER;
 * PY32 has no PORTCON). */
static inline void SDA_SetDir(bool Output)
{
	GPIOB->MODER &= ~(3u << (BK4819_SDA_PIN * 2u));
	if (Output)
		GPIOB->MODER |= (1u << (BK4819_SDA_PIN * 2u));
	/* Output == false: leave the field at 00 (input) — nothing more to set. */
}

/* Transcribed from armel bk4829.c:89-92 (SDA_ReadInput -> GPIO_IsInputPinSet
 * -> LL_GPIO_IsInputPinSet), reimplemented against GPIOB->IDR directly. */
static inline uint32_t SDA_ReadInput(void)
{
	return (GPIOB->IDR & (1u << BK4819_SDA_PIN)) ? 1u : 0u;
}

void BK4819_Init(void)
{
	bk4819_bus_gpio_init();

	CS_Release();
	SCL_Set();
	SDA_Set();

	BK4819_WriteRegister(BK4819_REG_00, 0x8000);
	BK4819_WriteRegister(BK4819_REG_00, 0x0000);

	BK4819_WriteRegister(BK4819_REG_37, 0x9D1F);
	BK4819_WriteRegister(BK4819_REG_36, 0x0022);

	// BK4819_InitAGC(false);
	// BK4819_SetAGC(true);
	BK4819_WriteRegister(BK4819_REG_10, 0x0318);
	BK4819_WriteRegister(BK4819_REG_11, 0x033A);
	BK4819_WriteRegister(BK4819_REG_12, 0x03DB);
	BK4819_WriteRegister(BK4819_REG_13, 0x03DF);
	BK4819_WriteRegister(BK4819_REG_14, 0x0210);
	BK4819_WriteRegister(BK4819_REG_49, 0x2AB2);
	BK4819_WriteRegister(BK4819_REG_7B, 0x73DC);

	// BK4819_WriteRegister(BK4819_REG_19, 0b0001000001000001);   // <15> MIC AGC  1 = disable  0 = enable

	BK4819_WriteRegister(BK4819_REG_7D, 0xE920);

	// REG_48 .. RX AF level
	//
	// <15:12> 11  ???  0 to 15
	//
	// <11:10> 0 AF Rx Gain-1
	//         0 =   0dB
	//         1 =  -6dB
	//         2 = -12dB
	//         3 = -18dB
	//
	// <9:4>   60 AF Rx Gain-2  -26dB ~ 5.5dB   0.5dB/step
	//         63 = max
	//          0 = mute
	//
	// <3:0>   15 AF DAC Gain (after Gain-1 and Gain-2) approx 2dB/step
	//         15 = max
	//          0 = min
	//
	BK4819_WriteRegister(BK4819_REG_48,	//  0xB3A8);     // 1011 00 111010 1000
		// (11u << 12) |     // ??? 0..15
		// ( 0u << 10) |     // AF Rx Gain-1
		// (58u <<  4) |     // AF Rx Gain-2
		// ( 8u <<  0));     // AF DAC Gain (after Gain-1 and Gain-2)
		0x33A8);

	BK4819_WriteRegister(0x40, 0x3516);

#if 1
	const uint8_t dtmf_coeffs[] = {111, 107, 103, 98, 80, 71, 58, 44, 65, 55, 37, 23, 228, 203, 181, 159};
	for (unsigned int i = 0; i < ARRAY_SIZE(dtmf_coeffs); i++)
		BK4819_WriteRegister(BK4819_REG_09, (i << 12) | dtmf_coeffs[i]);
#else
	// original code
	BK4819_WriteRegister(BK4819_REG_09, 0x006F);  // 6F
	BK4819_WriteRegister(BK4819_REG_09, 0x106B);  // 6B
	BK4819_WriteRegister(BK4819_REG_09, 0x2067);  // 67
	BK4819_WriteRegister(BK4819_REG_09, 0x3062);  // 62
	BK4819_WriteRegister(BK4819_REG_09, 0x4050);  // 50
	BK4819_WriteRegister(BK4819_REG_09, 0x5047);  // 47
	BK4819_WriteRegister(BK4819_REG_09, 0x603A);  // 3A
	BK4819_WriteRegister(BK4819_REG_09, 0x702C);  // 2C
	BK4819_WriteRegister(BK4819_REG_09, 0x8041);  // 41
	BK4819_WriteRegister(BK4819_REG_09, 0x9037);  // 37
	BK4819_WriteRegister(BK4819_REG_09, 0xA025);  // 25
	BK4819_WriteRegister(BK4819_REG_09, 0xB017);  // 17
	BK4819_WriteRegister(BK4819_REG_09, 0xC0E4);  // E4
	BK4819_WriteRegister(BK4819_REG_09, 0xD0CB);  // CB
	BK4819_WriteRegister(BK4819_REG_09, 0xE0B5);  // B5
	BK4819_WriteRegister(BK4819_REG_09, 0xF09F);  // 9F
#endif

	BK4819_WriteRegister(0x1C, 0x07C0);
	BK4819_WriteRegister(0x1D, 0xE555);
	BK4819_WriteRegister(0x1E, 0x4C58);

	BK4819_WriteRegister(BK4819_REG_1F, 0xC65A);
	BK4819_WriteRegister(BK4819_REG_3E, 0x94C6);

	BK4819_WriteRegister(0x73, 0x4691);
	BK4819_WriteRegister(0x77, 0x88EF);
	BK4819_WriteRegister(BK4819_REG_19, 0x1041);
	BK4819_WriteRegister(BK4819_REG_28, 0x0B40);
	BK4819_WriteRegister(BK4819_REG_29, 0xAA00);
	BK4819_WriteRegister(0x2A, 0x6600);
	BK4819_WriteRegister(0x2C, 0x1822);
	BK4819_WriteRegister(0x2F, 0x9890);
	BK4819_WriteRegister(0x53, 0x2028);
	BK4819_WriteRegister(BK4819_REG_7E, 0x303E);
	BK4819_WriteRegister(BK4819_REG_46, 0x600A);
	BK4819_WriteRegister(0x4A, 0x5430);
	BK4819_WriteRegister(BK4819_REG_07, 0x61CE);

	gBK4819_GpioOutState = 0x9000;

	BK4819_WriteRegister(BK4819_REG_33, 0x9000);
	BK4819_WriteRegister(BK4819_REG_3F, 0);
}

/* Transcribed from armel bk4829.c:204-224 onto the helpers above.
 * Structurally identical to V1's BK4819_ReadU16 (same 16-bit MSB-first
 * clock-out-on-SCL-high shape); only the direction-flip and per-edge
 * delay primitives changed. */
static uint16_t BK4819_ReadU16(void)
{
	unsigned int i;
	uint16_t     Value;

	SDA_SetDir(false);
	SHORT_DELAY();
	Value = 0;
	for (i = 0; i < 16; i++)
	{
		Value <<= 1;
		Value |= SDA_ReadInput();
		SCL_Set();
		SHORT_DELAY();
		SCL_Reset();
		SHORT_DELAY();
	}
	SDA_SetDir(true);

	return Value;
}

uint16_t BK4819_ReadRegister(BK4819_REGISTER_t Register)
{
	uint16_t Value;

	CS_Release();
	SCL_Reset();

	SHORT_DELAY();

	CS_Assert();
	BK4819_WriteU8(Register | 0x80);
	Value = BK4819_ReadU16();
	CS_Release();

	SHORT_DELAY();

	SCL_Set();
	SDA_Set();

	return Value;
}

void BK4819_WriteRegister(BK4819_REGISTER_t Register, uint16_t Data)
{
	CS_Release();
	SCL_Reset();

	SHORT_DELAY();

	CS_Assert();
	BK4819_WriteU8(Register);

	SHORT_DELAY();

	BK4819_WriteU16(Data);

	SHORT_DELAY();

	CS_Release();

	SHORT_DELAY();

	SCL_Set();
	SDA_Set();
}

void BK4819_WriteU8(uint8_t Data)
{
	unsigned int i;

	SCL_Reset();
	for (i = 0; i < 8; i++)
	{
		if ((Data & 0x80) == 0)
			SDA_Reset();
		else
			SDA_Set();

		SHORT_DELAY();
		SCL_Set();
		SHORT_DELAY();

		Data <<= 1;

		SCL_Reset();
		SHORT_DELAY();
	}
}

void BK4819_WriteU16(uint16_t Data)
{
	unsigned int i;

	SCL_Reset();
	for (i = 0; i < 16; i++)
	{
		if ((Data & 0x8000) == 0)
			SDA_Reset();
		else
			SDA_Set();

		SHORT_DELAY();
		SCL_Set();

		Data <<= 1;

		SHORT_DELAY();
		SCL_Reset();
		SHORT_DELAY();
	}
}

void BK4819_RX_TurnOn(void)
{
	// DSP Voltage Setting = 1
	// ANA LDO = 2.7v
	// VCO LDO = 2.7v
	// RF LDO  = 2.7v
	// PLL LDO = 2.7v
	// ANA LDO bypass
	// VCO LDO bypass
	// RF LDO  bypass
	// PLL LDO bypass
	// Reserved bit is 1 instead of 0
	// Enable  DSP
	// Enable  XTAL
	// Enable  Band Gap
	//
	BK4819_WriteRegister(BK4819_REG_37, 0x9F1F);  // 0001111100001111

	// Turn off everything
	BK4819_WriteRegister(BK4819_REG_30, 0);


	BK4819_WriteRegister(BK4819_REG_30, 0xBFF1);
		// BK4819_REG_30_ENABLE_VCO_CALIB |
		// BK4819_REG_30_DISABLE_UNKNOWN |
		// BK4819_REG_30_ENABLE_RX_LINK |
		// BK4819_REG_30_ENABLE_AF_DAC |
		// BK4819_REG_30_ENABLE_DISC_MODE |
		// BK4819_REG_30_ENABLE_PLL_VCO |
		// BK4819_REG_30_DISABLE_PA_GAIN |
		// BK4819_REG_30_DISABLE_MIC_ADC |
		// BK4819_REG_30_DISABLE_TX_DSP |
		// BK4819_REG_30_ENABLE_RX_DSP );
}

/* ---- Chip tables that differ between the V1 and V3 BK4819 variants ----
 * The V3 board carries the BK4829-flavoured chip. These values are taken
 * verbatim from armel's known-good V3 driver (App/driver/bk4829.c) — the
 * same lesson as the RX fix: V1's values on this chip misbehave silently.
 */

void BK4819_SetFilterBandwidth(const BK4819_FilterBandwidth_t Bandwidth, const bool weak_no_different)
{
	(void)weak_no_different;
	uint16_t val;
	switch (Bandwidth)
	{
		case BK4819_FILTER_BW_WIDE:     val = 0x3028; break;   /* armel */
		case BK4819_FILTER_BW_NARROW:   val = 0x4048; break;   /* armel */
		case BK4819_FILTER_BW_NARROWER: val = 0x2058; break;   /* armel */
		/* armel has no 1.7 kHz mode; keep V1's value for it. */
		case BK4819_FILTER_BW_U1K7:     val = 0x0058; break;
		default:                        val = 0x005C; break;   /* armel default */
	}
	BK4819_WriteRegister(BK4819_REG_43, val);
}

void BK4819_EnterTxMute(void)
{
	BK4819_WriteRegister(BK4819_REG_50, 0xBB18);
}

void BK4819_ExitTxMute(void)
{
	BK4819_WriteRegister(BK4819_REG_50, 0x3B18);
}

void BK4819_TxOn_Beep(void)
{
	BK4819_WriteRegister(BK4819_REG_36, 0);
	BK4819_WriteRegister(BK4819_REG_37, 0x9D1F);
	BK4819_WriteRegister(BK4819_REG_52, 0x028F);
	BK4819_WriteRegister(BK4819_REG_30, 0x0000);
	BK4819_WriteRegister(BK4819_REG_30, 0xC1FE);
}
