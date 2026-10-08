/* V3 SPI NOR flash (PY25Q16), read-only — see Inc/peripherals/spi_flash.h.
 *
 * Wiring from armel App/driver/py25q16.c (muzkr): SPI2 with SCK = PA0 (AF8),
 * MOSI = PA1 and MISO = PA2 (AF9), chip select = PA3 (GPIO, active low),
 * SPI mode 3. Polled byte transfers instead of armel's DMA: reads here are
 * a few bytes at boot, so DMA buys nothing.
 */
#include "peripherals/spi_flash.h"
#include "py32f0xx.h"

#define FLASH_CS_PIN 3u   /* PA3 */
#define CMD_READ     0x03u

static void cs_low(void)  { GPIOA->BSRR = 1u << (FLASH_CS_PIN + 16u); }
static void cs_high(void) { GPIOA->BSRR = 1u << FLASH_CS_PIN; }

static uint8_t xfer(uint8_t b)
{
    while (!(SPI2->SR & SPI_SR_TXE))
    {
    }
    *(volatile uint8_t *)&SPI2->DR = b;   /* 8-bit access: one frame */
    while (!(SPI2->SR & SPI_SR_RXNE))
    {
    }
    return *(volatile uint8_t *)&SPI2->DR;
}

void SPI_FLASH_Init(void)
{
    RCC->IOPENR  |= RCC_IOPENR_GPIOAEN;
    RCC->APBENR1 |= RCC_APBENR1_SPI2EN;
    (void)RCC->APBENR1;   /* read-back: ensure the enables took effect */

    /* PA3 = CS: push-pull output, idle high. Set high BEFORE switching to
     * output so the flash never sees a spurious select. */
    cs_high();
    GPIOA->MODER &= ~(3u << (FLASH_CS_PIN * 2u));
    GPIOA->MODER |=  (1u << (FLASH_CS_PIN * 2u));
    GPIOA->OTYPER &= ~(1u << FLASH_CS_PIN);

    /* PA0..PA2: alternate function, very high speed, pull-up (as armel). */
    GPIOA->MODER   &= ~((3u << 0) | (3u << 2) | (3u << 4));
    GPIOA->MODER   |=  ((2u << 0) | (2u << 2) | (2u << 4));
    GPIOA->OTYPER  &= ~((1u << 0) | (1u << 1) | (1u << 2));
    GPIOA->OSPEEDR |=  ((3u << 0) | (3u << 2) | (3u << 4));
    GPIOA->PUPDR   &= ~((3u << 0) | (3u << 2) | (3u << 4));
    GPIOA->PUPDR   |=  ((1u << 0) | (1u << 2) | (1u << 4));
    GPIOA->AFR[0]  &= ~((0xFu << 0) | (0xFu << 4) | (0xFu << 8));
    GPIOA->AFR[0]  |=  ((8u << 0) | (9u << 4) | (9u << 8));   /* AF8, AF9, AF9 */

    /* Master, mode 3 (CPOL=1, CPHA=1), soft NSS, BR = DIV8 -> 6 MHz
     * (armel runs DIV2; slower is plenty for a few bytes). */
    SPI2->CR1 = SPI_CR1_CPHA | SPI_CR1_CPOL | SPI_CR1_MSTR
              | SPI_CR1_BR_1 | SPI_CR1_SSM | SPI_CR1_SSI;
    SPI2->CR1 |= SPI_CR1_SPE;
}

void SPI_FLASH_Read(uint32_t addr, void *buf, uint32_t size)
{
    uint8_t *p = buf;

    cs_low();
    xfer(CMD_READ);
    xfer((uint8_t)(addr >> 16));
    xfer((uint8_t)(addr >> 8));
    xfer((uint8_t)addr);
    for (uint32_t i = 0; i < size; i++)
        p[i] = xfer(0xFF);
    while (SPI2->SR & SPI_SR_BSY)
    {
    }
    cs_high();
}
