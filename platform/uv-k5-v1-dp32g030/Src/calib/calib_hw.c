/* V1: factory calibration lives in EEPROM 0x1E00..0x1FFF. */
#include "calib/calib.h"
#include "peripherals/eeprom.h"

void CAL_HW_Read(uint16_t offset, void *buf, uint8_t len)
{
	EEPROM_ReadBuffer((uint16_t)(0x1E00u + offset), buf, len);
}
