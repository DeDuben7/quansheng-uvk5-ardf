#ifndef SPI_FLASH_H
#define SPI_FLASH_H

/* V3 on-board SPI NOR flash (PY25Q16, 2 MB) — READ-ONLY access.
 *
 * The flash holds the radio's factory calibration (0x010000..0x0101FF, same
 * layout as V1's EEPROM 0x1E00..0x1FFF) and the stock firmware's channels and
 * settings. This driver deliberately has no write or erase functions.
 */

#include <stdint.h>

void SPI_FLASH_Init(void);
void SPI_FLASH_Read(uint32_t addr, void *buf, uint32_t size);

#endif
