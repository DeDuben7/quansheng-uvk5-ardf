/* V3: factory calibration lives in SPI flash 0x010000..0x0101FF (armel
 * App/settings.c reads TX power at 0x0100D0 and battery at 0x010140). */
#include "calib/calib.h"
#include "peripherals/spi_flash.h"

void CAL_HW_Read(uint16_t offset, void *buf, uint8_t len)
{
    static bool inited;
    if (!inited)
    {
        SPI_FLASH_Init();
        inited = true;
    }
    SPI_FLASH_Read(0x010000u + offset, buf, len);
}
