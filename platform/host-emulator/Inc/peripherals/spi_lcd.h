/* Host emulator: LCD bus HAL. The bytes go into an ST7565 model
 * (Src/emu_lcd.c) instead of a real panel. Same contract as V3's header. */
#ifndef PERIPHERALS_SPI_LCD_H
#define PERIPHERALS_SPI_LCD_H

#include <stdint.h>

void SPI_LCD_Init(void);
void SPI_LCD_WriteByte(uint8_t b);
void LCD_CS_Low(void);
void LCD_CS_High(void);
void LCD_A0_Command(void);
void LCD_A0_Data(void);

#endif /* PERIPHERALS_SPI_LCD_H */
