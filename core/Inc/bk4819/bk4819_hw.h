#ifndef BK4819_HW_H
#define BK4819_HW_H

/* Platform contract for the shared BK4819 driver (core/Src/bk4819/bk4819.c).
 *
 * Two things legitimately differ per hardware version:
 *   1. The bus. V1 and V3 bit-bang the chip's 3-wire interface on different
 *      GPIOs with different timing primitives.
 *   2. The chip bring-up tables. The V1 board and the V3 board carry different
 *      BK4819 variants which need different register values in Init/RX_TurnOn
 *      (getting this wrong makes RSSI read 0 — see the V3 port notes).
 *      The same applies to the filter-bandwidth, TX-mute and TX-on tables
 *      (REG_43 / REG_50 / REG_37), so those are platform code too.
 * Everything else (tune, RSSI, AF mux, filters, squelch, GPIO, tones) is shared.
 *
 * gBK4819_GpioOutState is defined (non-static) in core/Src/bk4819/bk4819.c.
 * It is written by BK4819_Init() (platform code, via BK4819_WriteRegister)
 * and read/modified by BK4819_ToggleGpioOut() (core code), so it must be a
 * single shared instance rather than duplicated per platform. Declared
 * extern here so both sides see the same variable.
 */

#include <stdint.h>
#include "bk4819/bk4819_regs.h"

extern uint16_t gBK4819_GpioOutState;

void     BK4819_Init(void);
void     BK4819_RX_TurnOn(void);
/* Also platform-provided (declared in bk4819.h): BK4819_SetFilterBandwidth,
 * BK4819_EnterTxMute, BK4819_ExitTxMute, BK4819_TxOn_Beep. */
void     BK4819_WriteRegister(BK4819_REGISTER_t reg, uint16_t val);
uint16_t BK4819_ReadRegister(BK4819_REGISTER_t reg);

#endif
