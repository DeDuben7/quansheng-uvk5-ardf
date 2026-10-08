/* BK4819 fake: a register file. Writes are remembered and read back, with
 * three exceptions that make the radio logic testable:
 *   REG_67  returns the RSSI the scene set (`rssi <n>`), or a station's
 *           RSSI when the tuned frequency (REG_38/39) is within 6.25 kHz
 *           of one (`signal <hz> <rssi>`);
 *   REG_0C  bit 0 = interrupt pending (`sql open|close` raised one);
 *   REG_02  a write latches the pending flags into REG_02 and clears them —
 *           the same write-0-then-read sequence reald's CheckRadioInterrupts
 *           (and radio_squelch_task) use.
 * Interrupts are only raised when enabled in the REG_3F mask (same bits).
 *
 * Squelch model: `sql open|close` puts a carrier on or off the air and
 * raises the matching edge. BK4819_RX_TurnOn (which BK4819_SetupSquelch
 * calls) restarts the chip's squelch closed; if a carrier is on the air
 * it opens again a moment later, raising SQUELCH_LOST - so code that
 * reprograms the squelch must expect that edge, as on the real chip.
 */

#include <string.h>

#include "bk4819/bk4819.h"
#include "bk4819/bk4819_hw.h"
#include "emu/emu.h"

static uint16_t s_regs[128];
static uint16_t s_rssi = 60;    /* a quiet band */
static uint16_t s_irq;          /* pending REG_02 flags */
static bool     s_carrier;      /* `sql open` until `sql close` */
static bool     s_reevaluate;   /* RX restarted: re-check the carrier */
static unsigned s_rx_restarts;  /* BK4819_RX_TurnOn calls (`count reset`) */

#define MAX_STATIONS 8
static struct { uint32_t f10; uint16_t rssi; } s_stations[MAX_STATIONS];
static int s_station_count;

static uint16_t rssi_here(void)
{
    const uint32_t f10 = (uint32_t)s_regs[0x38] | ((uint32_t)s_regs[0x39] << 16);
    for (int i = 0; i < s_station_count; i++)
    {
        const uint32_t d = (f10 > s_stations[i].f10) ? f10 - s_stations[i].f10
                                                     : s_stations[i].f10 - f10;
        if (d <= 625u)   /* 6.25 kHz in 10 Hz units */
            return s_stations[i].rssi;
    }
    return s_rssi;
}

void BK4819_Init(void)
{
    memset(s_regs, 0, sizeof(s_regs));
    s_irq = 0;
}

void BK4819_RX_TurnOn(void)
{
    s_reevaluate = true;
    s_rx_restarts++;
}

unsigned emu_bk_rx_restarts(void)  { return s_rx_restarts; }
void     emu_bk_reset_counts(void) { s_rx_restarts = 0; }

void BK4819_WriteRegister(BK4819_REGISTER_t reg, uint16_t val)
{
    if (reg == BK4819_REG_02)
    {
        s_regs[0x02] = s_irq;
        s_irq = 0;
        return;
    }
    s_regs[reg & 0x7Fu] = val;
}

uint16_t BK4819_ReadRegister(BK4819_REGISTER_t reg)
{
    if (reg == BK4819_REG_0C) return s_irq ? 1u : 0u;
    if (reg == BK4819_REG_67) return rssi_here() & 0x01FFu;
    return s_regs[reg & 0x7Fu];
}

/* Platform-provided parts of the driver (declared in bk4819.h). The
 * register values don't matter here; they're V3's, so writes stay visible. */
void BK4819_SetFilterBandwidth(const BK4819_FilterBandwidth_t Bandwidth, const bool weak_no_different)
{
    (void)weak_no_different;
    BK4819_WriteRegister(BK4819_REG_43, (uint16_t)Bandwidth);
}

void BK4819_EnterTxMute(void)
{
    BK4819_WriteRegister(BK4819_REG_50, 0xBB18);
}

void BK4819_ExitTxMute(void)
{
    BK4819_WriteRegister(BK4819_REG_50, 0x3B18);
}

void BK4819_TxOn_Beep(void)
{
}

void emu_bk_set_rssi(uint16_t rssi)
{
    s_rssi = rssi;
}

void emu_bk_add_station(uint32_t hz, uint16_t rssi)
{
    if (s_station_count < MAX_STATIONS)
    {
        s_stations[s_station_count].f10  = hz / 10u;
        s_stations[s_station_count].rssi = rssi;
        s_station_count++;
    }
}

void emu_bk_raise_irq(uint16_t reg02_flags)
{
    s_irq |= (uint16_t)(reg02_flags & s_regs[0x3F]);
}

void emu_bk_set_carrier(bool on)
{
    s_carrier = on;
    emu_bk_raise_irq(on ? BK4819_REG_02_SQUELCH_LOST : BK4819_REG_02_SQUELCH_FOUND);
}

void emu_bk_tick(void)
{
    if (s_reevaluate)
    {
        s_reevaluate = false;
        if (s_carrier)
            emu_bk_raise_irq(BK4819_REG_02_SQUELCH_LOST);
    }
}

uint16_t emu_bk_reg(uint8_t reg)
{
    return s_regs[reg & 0x7Fu];
}
