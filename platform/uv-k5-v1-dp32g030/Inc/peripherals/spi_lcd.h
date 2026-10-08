#ifndef PERIPHERALS_SPI_LCD_H
#define PERIPHERALS_SPI_LCD_H

/* V1 (DP32G030) LCD HAL — the contract core/Src/st7565/st7565.c requires.
 *
 * V1 frames transfers with the SPI peripheral's hardware NSS (toggling master
 * mode) rather than a manual chip-select pin, so LCD_CS_Low/High wrap that
 * mechanism instead of driving a GPIO. A0 is GPIOB pin 9; there is a reset
 * line (GPIOB pin 11) and this platform genuinely pulses it.
 *
 * ST7565_HardwareReset() is also part of this LCD HAL contract (declared in
 * st7565/st7565.h, implemented per-platform) — see its definition in
 * Src/peripherals/spi_lcd.c. */

#include <stdint.h>

void SPI_LCD_Init(void);
void SPI_LCD_WriteByte(uint8_t b);
void LCD_CS_Low(void);
void LCD_CS_High(void);
void LCD_A0_Command(void);
void LCD_A0_Data(void);

#endif
