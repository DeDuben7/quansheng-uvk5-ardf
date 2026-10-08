/* SPI1 + LCD control GPIO HAL for the ST7565 panel on UV-K5 V3 (PY32F071).
 *
 * Pinout (transcribed from armel's App/driver/st7565.c):
 *   SCK  = PA5 (AF0/SPI1)
 *   MOSI = PA7 (AF0/SPI1)   ("SDA" in armel; MISO unused, LCD is write-only)
 *   CS   = PB2 (plain GPIO output, active-low, manual — SPI1 NSS is SOFT)
 *   A0   = PA6 (plain GPIO output; 0 = command, 1 = data)
 * There is no LCD reset line on this board (armel: "Not supported on K1").
 *
 * ST7565_HardwareReset() is also part of this LCD HAL contract (declared in
 * st7565/st7565.h, implemented per-platform) — see its (no-op) definition in
 * Src/peripherals/spi_lcd.c.
 */
#ifndef PERIPHERALS_SPI_LCD_H
#define PERIPHERALS_SPI_LCD_H

#include <stdint.h>

/* Enables GPIOA/GPIOB clocks, configures PA5/PA7 as AF0 (SPI1), PB2+PA6 as
 * plain outputs, configures and enables SPI1. Call once at boot. */
void SPI_LCD_Init(void);

/* Blocking single-byte SPI1 transfer (waits TXE, writes DR, waits RXNE,
 * drains DR — the LCD is write-only so the received byte is discarded). */
void SPI_LCD_WriteByte(uint8_t b);

/* LCD chip-select, PB2, active-low. */
void LCD_CS_Low(void);
void LCD_CS_High(void);

/* LCD A0 (command/data select), PA6. */
void LCD_A0_Command(void);
void LCD_A0_Data(void);

#endif /* PERIPHERALS_SPI_LCD_H */
