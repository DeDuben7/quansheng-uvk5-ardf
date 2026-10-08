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

/* V1 (DP32G030) LCD HAL implementation. Relocated verbatim from the old
 * platform/uv-k5-v1-dp32g030/Src/st7565/st7565.c (pre-refactor): the SPI0
 * init sequence, the FIFO-polled byte write (including the undocumented TX
 * FIFO status bit drain wait), the hardware-NSS chip-select toggle, and the
 * A0 GPIO writes. No register values or transfer sequencing changed — only
 * moved behind the HAL contract in Inc/peripherals/spi_lcd.h. */

#include "bsp/dp32g030/gpio.h"
#include "bsp/dp32g030/spi.h"
#include "peripherals/gpio.h"
#include "peripherals/spi.h"
#include "peripherals/spi_lcd.h"
#include "peripherals/system.h"
#include "st7565/st7565.h"

void SPI_LCD_Init(void)
{
	SPI0_Init();
}

void SPI_LCD_WriteByte(uint8_t b)
{
	while ((SPI0->FIFOST & SPI_FIFOST_TFF_MASK) != SPI_FIFOST_TFF_BITS_NOT_FULL) {}
	SPI0->WDR = b;
	SPI_WaitForUndocumentedTxFifoStatusBit();
}

void LCD_CS_Low(void)
{
	SPI_ToggleMasterMode(&SPI0->CR, false);
}

void LCD_CS_High(void)
{
	SPI_ToggleMasterMode(&SPI0->CR, true);
}

void LCD_A0_Command(void)
{
	GPIO_ClearBit(&GPIOB->DATA, GPIOB_PIN_ST7565_A0);
}

void LCD_A0_Data(void)
{
	GPIO_SetBit(&GPIOB->DATA, GPIOB_PIN_ST7565_A0);
}

/* V1 has a genuine LCD reset line (GPIOB pin 11); pulse it. Relocated
 * verbatim from the pre-refactor platform/uv-k5-v1-dp32g030/Src/st7565/
 * st7565.c (git c8ebb5d) — same register accesses and delay values. */
void ST7565_HardwareReset(void)
{
	GPIO_SetBit(&GPIOB->DATA, GPIOB_PIN_ST7565_RES);
	SYSTEM_DelayMs(1);
	GPIO_ClearBit(&GPIOB->DATA, GPIOB_PIN_ST7565_RES);
	SYSTEM_DelayMs(20);
	GPIO_SetBit(&GPIOB->DATA, GPIOB_PIN_ST7565_RES);
	SYSTEM_DelayMs(120);
}
