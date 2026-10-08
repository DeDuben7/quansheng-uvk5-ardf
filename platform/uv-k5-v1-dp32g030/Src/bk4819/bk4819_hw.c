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

/* V1 (DP32G030) platform layer for the shared BK4819 driver
 * (core/Src/bk4819/bk4819.c). Bit-bangs the chip's 3-wire bus on DP32
 * GPIOC0/1/2 (SCN/SCL/SDA) and carries this board's chip bring-up register
 * tables (BK4819_Init / BK4819_RX_TurnOn). Moved verbatim out of this
 * platform's old Src/bk4819/bk4819.c (Phase C bk4819.c split) — no register
 * values here changed.
 */

#include <stdbool.h>
#include <stdint.h>

#include "bk4819/bk4819.h"
#include "bk4819/bk4819_hw.h"
#include "bsp/dp32g030/gpio.h"
#include "bsp/dp32g030/portcon.h"
#include "peripherals/gpio.h"
#include "peripherals/systick.h"

#ifndef ARRAY_SIZE
	#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

void BK4819_Init(void)
{
	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);

	BK4819_WriteRegister(BK4819_REG_00, 0x8000);
	BK4819_WriteRegister(BK4819_REG_00, 0x0000);

	BK4819_WriteRegister(BK4819_REG_37, 0x1D0F);
	BK4819_WriteRegister(BK4819_REG_36, 0x0022);

	BK4819_InitAGC(false);
	BK4819_SetAGC(true);

	BK4819_WriteRegister(BK4819_REG_19, 0b0001000001000001);   // <15> MIC AGC  1 = disable  0 = enable

	BK4819_WriteRegister(BK4819_REG_7D, 0xE940);

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
		(11u << 12) |     // ??? 0..15
		( 0u << 10) |     // AF Rx Gain-1
		(58u <<  4) |     // AF Rx Gain-2
		( 8u <<  0));     // AF DAC Gain (after Gain-1 and Gain-2)

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

	BK4819_WriteRegister(BK4819_REG_1F, 0x5454);
	BK4819_WriteRegister(BK4819_REG_3E, 0xA037);

	gBK4819_GpioOutState = 0x9000;

	BK4819_WriteRegister(BK4819_REG_33, 0x9000);
	BK4819_WriteRegister(BK4819_REG_3F, 0);
}

static uint16_t BK4819_ReadU16(void)
{
	unsigned int i;
	uint16_t     Value;

	PORTCON_PORTC_IE = (PORTCON_PORTC_IE & ~PORTCON_PORTC_IE_C2_MASK) | PORTCON_PORTC_IE_C2_BITS_ENABLE;
	GPIOC->DIR = (GPIOC->DIR & ~GPIO_DIR_2_MASK) | GPIO_DIR_2_BITS_INPUT;
	SYSTICK_DelayUs(1);
	Value = 0;
	for (i = 0; i < 16; i++)
	{
		Value <<= 1;
		Value |= GPIO_CheckBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
		GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
		SYSTICK_DelayUs(1);
		GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
		SYSTICK_DelayUs(1);
	}
	PORTCON_PORTC_IE = (PORTCON_PORTC_IE & ~PORTCON_PORTC_IE_C2_MASK) | PORTCON_PORTC_IE_C2_BITS_DISABLE;
	GPIOC->DIR = (GPIOC->DIR & ~GPIO_DIR_2_MASK) | GPIO_DIR_2_BITS_OUTPUT;

	return Value;
}

uint16_t BK4819_ReadRegister(BK4819_REGISTER_t Register)
{
	uint16_t Value;

	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
	GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);

	SYSTICK_DelayUs(1);

	GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
	BK4819_WriteU8(Register | 0x80);
	Value = BK4819_ReadU16();
	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);

	SYSTICK_DelayUs(1);

	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);

	return Value;
}

void BK4819_WriteRegister(BK4819_REGISTER_t Register, uint16_t Data)
{
	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
	GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);

	SYSTICK_DelayUs(1);

	GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
	BK4819_WriteU8(Register);

	SYSTICK_DelayUs(1);

	BK4819_WriteU16(Data);

	SYSTICK_DelayUs(1);

	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);

	SYSTICK_DelayUs(1);

	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
}

void BK4819_WriteU8(uint8_t Data)
{
	unsigned int i;

	GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
	for (i = 0; i < 8; i++)
	{
		if ((Data & 0x80) == 0)
			GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
		else
			GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);

		SYSTICK_DelayUs(1);
		GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
		SYSTICK_DelayUs(1);

		Data <<= 1;

		GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
		SYSTICK_DelayUs(1);
	}
}

void BK4819_WriteU16(uint16_t Data)
{
	unsigned int i;

	GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
	for (i = 0; i < 16; i++)
	{
		if ((Data & 0x8000) == 0)
			GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
		else
			GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);

		SYSTICK_DelayUs(1);
		GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);

		Data <<= 1;

		SYSTICK_DelayUs(1);
		GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
		SYSTICK_DelayUs(1);
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
	BK4819_WriteRegister(BK4819_REG_37, 0x1F0F);  // 0001111100001111

	// Turn off everything
	BK4819_WriteRegister(BK4819_REG_30, 0);


	BK4819_WriteRegister(BK4819_REG_30,
		BK4819_REG_30_ENABLE_VCO_CALIB |
		BK4819_REG_30_DISABLE_UNKNOWN |
		BK4819_REG_30_ENABLE_RX_LINK |
		BK4819_REG_30_ENABLE_AF_DAC |
		BK4819_REG_30_ENABLE_DISC_MODE |
		BK4819_REG_30_ENABLE_PLL_VCO |
		BK4819_REG_30_DISABLE_PA_GAIN |
		BK4819_REG_30_DISABLE_MIC_ADC |
		BK4819_REG_30_DISABLE_TX_DSP |
		BK4819_REG_30_ENABLE_RX_DSP );
}

/* ---- Chip tables that differ between the V1 and V3 BK4819 variants ----
 * Moved out of core for the TX work: V3 needs its own REG_43 / REG_50 /
 * REG_37 values (see the V3 copy of this block). V1 values unchanged.
 */

void BK4819_SetFilterBandwidth(const BK4819_FilterBandwidth_t Bandwidth, const bool weak_no_different)
{
	// REG_43
	// <15>    0 ???
	//
	// <14:12> 4 RF filter bandwidth
	//         0 = 1.7  kHz
	//         1 = 2.0  kHz
	//         2 = 2.5  kHz
	//         3 = 3.0  kHz
	//         4 = 3.75 kHz
	//         5 = 4.0  kHz
	//         6 = 4.25 kHz
	//         7 = 4.5  kHz
	// if <5> == 1, RF filter bandwidth * 2
	//
	// <11:9>  0 RF filter bandwidth when signal is weak
	//         0 = 1.7  kHz
	//         1 = 2.0  kHz
	//         2 = 2.5  kHz
	//         3 = 3.0  kHz
	//         4 = 3.75 kHz
	//         5 = 4.0  kHz
	//         6 = 4.25 kHz
	//         7 = 4.5  kHz
	// if <5> == 1, RF filter bandwidth * 2
	//
	// <8:6>   1 AFTxLPF2 filter Band Width
	//         1 = 2.5  kHz (for 12.5k channel space)
	//         2 = 2.75 kHz
	//         0 = 3.0  kHz (for 25k   channel space)
	//         3 = 3.5  kHz
	//         4 = 4.5  kHz
	//         5 = 4.25 kHz
	//         6 = 4.0  kHz
	//         7 = 3.75 kHz
	//
	// <5:4>   0 BW Mode Selection
	//         0 = 12.5k
	//         1 =  6.25k
	//         2 = 25k/20k
	//
	// <3>     1 ???
	//
	// <2>     0 Gain after FM Demodulation
	//         0 = 0dB
	//         1 = 6dB
	//
	// <1:0>   0 ???

	uint16_t val = 0;
	switch (Bandwidth)
	{
		default:
		case BK4819_FILTER_BW_WIDE:	// 25kHz
			val = (4u << 12) |     // *3 RF filter bandwidth
				  (6u <<  6) |     // *0 AFTxLPF2 filter Band Width
				  (2u <<  4) |     //  2 BW Mode Selection
				  (1u <<  3) |     //  1
				  (0u <<  2);     //  0 Gain after FM Demodulation

			if (weak_no_different) {
				// make the RX bandwidth the same with weak signals
				val |= (4u <<  9);     // *0 RF filter bandwidth when signal is weak
			} else {
				/// with weak RX signals the RX bandwidth is reduced
				val |= (2u <<  9);     // *0 RF filter bandwidth when signal is weak
			}

			break;

		case BK4819_FILTER_BW_NARROW:	// 12.5kHz
			val = (4u << 12) |     // *4 RF filter bandwidth
				  (0u <<  6) |     // *1 AFTxLPF2 filter Band Width
				  (0u <<  4) |     //  0 BW Mode Selection
				  (1u <<  3) |     //  1
				  (0u <<  2);      //  0 Gain after FM Demodulation

			if (weak_no_different) {
				val |= (4u <<  9);     // *0 RF filter bandwidth when signal is weak
			} else {
				val |= (2u <<  9);
			}

			break;

/*		case BK4819_FILTER_BW_NARROWER:	// 6.25kHz
			val = (3u << 12) |     //  3 RF filter bandwidth
				  (1u <<  6) |     //  1 AFTxLPF2 filter Band Width
				  (1u <<  4) |     //  1 BW Mode Selection
				  (1u <<  3) |     //  1
				  (0u <<  2);      //  0 Gain after FM Demodulation

			if (weak_no_different) {
				val |= (3u <<  9);
			} else {
				val |= (0u <<  9);     //  0 RF filter bandwidth when signal is weak
			}
			break; */

                case BK4819_FILTER_BW_NARROWER:// 2.5 kHz modified for ARDF
			val = (2u << 12) |     //  RF filter bandwidth (010 = 2.5 kHz)
                              (2u <<  9) |     //  RF filter bandwidth when signal is weak, same (010 = 2.5 kHz)
                              (1u <<  6) |     //  AFTxLPF2 filter Band Width (smallest, but we don´t care about TX)
                              (1u <<  4) |     //  BW Mode Selection (00=12.5k; 01=6.25k; 10=25k/20k)
                              (1u <<  3) |     //  1
                              (0u <<  2);      //  Gain after FM Demodulation (1=6dB; 0=0 dB)

			break;

		case BK4819_FILTER_BW_U1K7:	// 1.7 kHz modified for ARDF
			val = (0u << 12) |     //  RF filter bandwidth (000 = 1.7 kHz)
                              (0u <<  9) |     //  RF filter bandwidth when signal is weak, same (000 = 1.7 kHz)
                              (1u <<  6) |     //  AFTxLPF2 filter Band Width (smallest, but we don´t care about TX)
                              (1u <<  4) |     //  BW Mode Selection (00=12.5k; 01=6.25k; 10=25k/20k)
                              (1u <<  3) |     //  1
                              (0u <<  2);      //  Gain after FM Demodulation (1=6dB; 0=0 dB)

			break;


	}

	BK4819_WriteRegister(BK4819_REG_43, val);
}

void BK4819_EnterTxMute(void)
{
	BK4819_WriteRegister(BK4819_REG_50, 0xBB20);
}

void BK4819_ExitTxMute(void)
{
	BK4819_WriteRegister(BK4819_REG_50, 0x3B20);
}

void BK4819_TxOn_Beep(void)
{
	BK4819_WriteRegister(BK4819_REG_37, 0x1D0F);
	BK4819_WriteRegister(BK4819_REG_52, 0x028F);
	BK4819_WriteRegister(BK4819_REG_30, 0x0000);
	BK4819_WriteRegister(BK4819_REG_30, 0xC1FE);
}
