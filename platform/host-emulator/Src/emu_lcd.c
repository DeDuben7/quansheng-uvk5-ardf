/* ST7565 model. The real core st7565.c driver talks to this through the
 * LCD HAL, so screens come out exactly as the radio draws them.
 *
 * Only what the driver uses is modelled: page select (0xB0|p), column
 * address high/low nibble (0x10|h, 0x00|l) and data writes with column
 * auto-increment. The driver offsets columns by 4 (132-column RAM, 128
 * visible). Every other command is ignored; 0x81 (set contrast) takes a
 * second byte, which is swallowed so it isn't mistaken for a command.
 */

#include <stdbool.h>
#include <stdint.h>

#include "emu/emu.h"
#include "peripherals/spi_lcd.h"
#include "st7565/st7565.h"

#define RAM_PAGES   8
#define RAM_COLUMNS 132
#define COLUMN_OFFSET 4

static uint8_t s_ram[RAM_PAGES][RAM_COLUMNS];
static uint8_t s_page;
static uint8_t s_col;
static bool    s_data_mode;
static bool    s_skip_next_command;   /* operand of 0x81 */

void SPI_LCD_Init(void)          {}
void LCD_CS_Low(void)            {}
void LCD_CS_High(void)           {}
void LCD_A0_Command(void)        { s_data_mode = false; }
void LCD_A0_Data(void)           { s_data_mode = true; }
void ST7565_HardwareReset(void)  {}

void SPI_LCD_WriteByte(uint8_t b)
{
    if (s_data_mode)
    {
        if (s_page < RAM_PAGES && s_col < RAM_COLUMNS)
            s_ram[s_page][s_col] = b;
        s_col++;
        return;
    }

    if (s_skip_next_command)
    {
        s_skip_next_command = false;
        return;
    }

    if (b == 0x81u)
        s_skip_next_command = true;
    else if ((b & 0xF0u) == 0xB0u)
        s_page = b & 0x0Fu;
    else if ((b & 0xF0u) == 0x10u)
        s_col = (uint8_t)((s_col & 0x0Fu) | ((b & 0x0Fu) << 4));
    else if ((b & 0xF0u) == 0x00u)
        s_col = (uint8_t)((s_col & 0xF0u) | (b & 0x0Fu));
}

bool emu_lcd_pixel(int x, int y)
{
    if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT)
        return false;
    return (s_ram[y >> 3][x + COLUMN_OFFSET] >> (y & 7)) & 1u;
}
