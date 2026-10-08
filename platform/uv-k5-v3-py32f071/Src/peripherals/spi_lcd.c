/* SPI1 + LCD control GPIO HAL for the ST7565 panel on UV-K5 V3 (PY32F071).
 *
 * The SPI1 register sequence below is TRANSCRIBED from armel's working V3
 * firmware, not invented: armel-k5v3-src/App/driver/st7565.c, static
 * function `SPI_Init()` (lines 37-76) and `SPI_WriteByte()` (lines 98-109).
 * Armel uses the PY32 LL driver
 * (`LL_SPI_InitTypeDef`/`LL_GPIO_InitTypeDef`); this file reproduces the
 * exact same CR1 bits and GPIO config directly against CMSIS registers,
 * matching this port's no-HAL, bare-register style (see main.c).
 *
 * Config transcribed from armel SPI_Init() (st7565.c:62-76):
 *   TransferDirection = FULL_DUPLEX      (reset default, not written)
 *   Mode              = MASTER           -> CR1.MSTR = 1
 *   DataWidth         = 8BIT             -> CR1.DFF = 0 (reset default)
 *   ClockPolarity     = HIGH  (CPOL=1)   -> CR1.CPOL = 1
 *   ClockPhase        = 2EDGE (CPHA=1)   -> CR1.CPHA = 1
 *   NSS               = SOFT             -> CR1.SSM = 1, CR1.SSI = 1
 *   BitOrder          = MSB_FIRST        -> CR1.LSBFIRST = 0 (reset default)
 *   CRCCalculation    = DISABLE          (reset default, not written)
 *   BaudRate          = DIV8 (6 MHz)     -> CR1.BR = 0b010 (BR_1)
 *                       (armel used DIV64/750kHz; raised for a faster blit)
 * then SPI1 is enabled (CR1.SPE = 1). CR2 is left at its reset value (0) —
 * armel's LL_SPI_InitTypeDef only ever touches SSOE for hardware-NSS modes,
 * and NSS here is SOFT, so CR2.SSOE must stay 0 (matches armel).
 *
 * armel's SPI_WriteByte() polls TXE, writes DR, then polls RXNE and reads
 * DR back (st7565.c:98-109) — reproduced verbatim below. The LCD has no
 * MISO connection; the received byte is meaningless and discarded, exactly
 * as armel does (every caller in st7565.c ignores SPI_WriteByte's return
 * value).
 *
 * CS (PB2) and A0 (PA6) are plain bit-banged GPIO outputs, matching armel's
 * CS_Assert/CS_Release/A0_Set/A0_Reset (st7565.c:78-96) — PY32 SPI1 is
 * configured NSS=SOFT so there is no hardware CS output to rely on.
 */
#include "peripherals/spi_lcd.h"
#include "py32f0xx.h"
#include "st7565/st7565.h"

#define LCD_CS_PIN   2u   /* PB2 */
#define LCD_A0_PIN   6u   /* PA6 */
#define LCD_SCK_PIN  5u   /* PA5, AF0/SPI1 */
#define LCD_SDA_PIN  7u   /* PA7 (MOSI), AF0/SPI1 */

static void spi_lcd_gpio_init(void)
{
    /* Clocks for both GPIO ports touched here (CS on GPIOB, everything
     * else on GPIOA). Armel enables these board-wide in BOARD_GPIO_Init();
     * this port has no board-init step yet, so SPI_LCD_Init does it. */
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN;
    (void)RCC->IOPENR; /* read-back: ensure the enable took effect */

    /* --- PA5 (SCK) / PA7 (MOSI): alternate function, AF0 = SPI1 --- */

    /* MODER = 10 (alternate function) for pins 5 and 7. */
    GPIOA->MODER &= ~((3u << (LCD_SCK_PIN * 2u)) | (3u << (LCD_SDA_PIN * 2u)));
    GPIOA->MODER |=  ((2u << (LCD_SCK_PIN * 2u)) | (2u << (LCD_SDA_PIN * 2u)));

    /* AFRL nibble = 0000 (AF0) for pins 5 and 7 — AF0 is value 0, so this
     * is a no-op numerically, written explicitly for clarity. */
    GPIOA->AFR[0] &= ~((0xFu << (LCD_SCK_PIN * 4u)) | (0xFu << (LCD_SDA_PIN * 4u)));

    /* Push-pull output type (armel: OUTPUT_PUSHPULL) — reset default 0. */
    GPIOA->OTYPER &= ~((1u << LCD_SCK_PIN) | (1u << LCD_SDA_PIN));

    /* Very-high speed (armel: SPEED_FREQ_VERY_HIGH) = 11 on both pins. */
    GPIOA->OSPEEDR |= (3u << (LCD_SCK_PIN * 2u)) | (3u << (LCD_SDA_PIN * 2u));

    /* Pull config: armel pulls SCK up, leaves MOSI with no pull. */
    GPIOA->PUPDR &= ~((3u << (LCD_SCK_PIN * 2u)) | (3u << (LCD_SDA_PIN * 2u)));
    GPIOA->PUPDR |=  (1u << (LCD_SCK_PIN * 2u)); /* PA5 pull-up */

    /* --- PA6 (A0) / PB2 (CS): plain GPIO outputs, push-pull --- */

    GPIOA->MODER &= ~(3u << (LCD_A0_PIN * 2u));
    GPIOA->MODER |=  (1u << (LCD_A0_PIN * 2u));
    GPIOA->OTYPER &= ~(1u << LCD_A0_PIN);

    GPIOB->MODER &= ~(3u << (LCD_CS_PIN * 2u));
    GPIOB->MODER |=  (1u << (LCD_CS_PIN * 2u));
    GPIOB->OTYPER &= ~(1u << LCD_CS_PIN);
}

static void spi_lcd_spi_init(void)
{
    RCC->APBENR2 |= RCC_APBENR2_SPI1EN;
    (void)RCC->APBENR2; /* read-back: ensure the enable took effect */

    /* Full CR1 assignment (not |=) so LSBFIRST/DFF land at their required
     * 0 values regardless of register reset state. */
    SPI1->CR1 = SPI_CR1_CPHA | SPI_CR1_CPOL | SPI_CR1_MSTR
              | SPI_CR1_BR_1   /* BR = 0b010 = DIV8 -> 48MHz/8 = 6 MHz.
                                * armel used DIV64 (~750 kHz) which made the
                                * full-screen blit ~9.5 ms of blocking SPI.
                                * 6 MHz is well within the ST7565's ~20 MHz
                                * spec; combined with dirty-page diffing in
                                * st7565.c the blit is now sub-millisecond. */
              | SPI_CR1_SSM | SPI_CR1_SSI;
    /* CR2 left at reset value (0): SSOE must stay clear for soft NSS. */

    SPI1->CR1 |= SPI_CR1_SPE;
}

void SPI_LCD_Init(void)
{
    spi_lcd_gpio_init();
    spi_lcd_spi_init();
}

void SPI_LCD_WriteByte(uint8_t b)
{
    while (!(SPI1->SR & SPI_SR_TXE))
    {
    }
    SPI1->DR = b;

    while (!(SPI1->SR & SPI_SR_RXNE))
    {
    }
    (void)SPI1->DR; /* drain RX; MISO is unconnected, value is meaningless */
}

void LCD_CS_Low(void)
{
    GPIOB->BSRR = (1u << (LCD_CS_PIN + 16u));
}

void LCD_CS_High(void)
{
    GPIOB->BSRR = (1u << LCD_CS_PIN);
}

void LCD_A0_Command(void)
{
    GPIOA->BSRR = (1u << (LCD_A0_PIN + 16u));
}

void LCD_A0_Data(void)
{
    GPIOA->BSRR = (1u << LCD_A0_PIN);
}

/* Deliberately a no-op: this board drives no /RES line; matches armel's
 * ST7565_HardwareReset() ("Not supported on K1"). */
void ST7565_HardwareReset(void)
{
}
