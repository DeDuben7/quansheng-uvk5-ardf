# scout — UV-K5 V1 (Nyquest DP32G030)

Platform layer for the **Quansheng UV-K5 hardware version 1** (DP32G030,
Cortex-M0). The MCU-agnostic application, UI and chip drivers live in
[`../../core/`](../../core/) and are compiled from here — see the
[root README](../../README.md) for the overall structure.

Status: **working, flashed.**

## What lives here

- `Src/peripherals/` — DP32G030 HAL: gpio, spi, spi_lcd (LCD HAL), i2c,
  eeprom, adc, keyboard, backlight, system, systick, aes, crc.
- `Src/bk4819/bk4819_hw.c` — BK4819 bus (bit-banged on GPIOC) + this chip's
  `Init`/`RX_TurnOn` register tables.
- `Src/board.c`, `Src/scheduler.c`, `Src/version.c`, `Src/main.c` — board
  bring-up, the 10 ms tick, and the boot sequence.
- `bsp/`, `hardware/*.def` — DP32G030 register definitions.
- `external/CMSIS_5/` — CMSIS headers (vendor).
- `start.S`, `init.c`, `firmware.ld` — startup and linker script.
- `fw-pack.py` — obfuscation + CRC packer the V1 bootloader requires.

Anything byte-identical to the other platforms belongs in `core/`, not here.

## Build

```bash
make
```

Produces `firmware_uvk5_v1` (ELF), `.bin`, and the flashable
`.packed.bin`. Objects go under `build/` (`build/core/` for shared sources,
`build/plat/` for this platform's).

Toolchain: arm-none-eabi-gcc 10.2.1, GNU Make, Python 3.8+ with `crcmod`
for the packer.

Note: `make clean` uses cmd.exe syntax and does nothing under Git Bash —
use `rm -rf build` there.

## Flashing

```powershell
K5TOOL.exe -port COMx -wrflash firmware_uvk5_v1.packed.bin
```

Note `-wrflash` with the **packed** image (V1's bootloader expects the
obfuscated format — unlike V3, which takes a raw binary). Hold PTT while
powering the radio on to enter bootloader mode (white torch LED must light).

## Reference

`../../../reald-src/` — the reald ARDF fork this port's drivers came from.
Read-only reference; never flashed as-is.
