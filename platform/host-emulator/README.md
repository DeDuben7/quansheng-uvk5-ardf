# scout — host display emulator

Runs the real `core/` firmware on the PC, so you can test firmware logic and
render the LCD without a radio. It is a third "platform": it implements the
HAL contract in `core/README.md` with fakes instead of hardware.

| HAL piece | Fake |
|---|---|
| LCD bus (`spi_lcd.h`) | An ST7565 model (`Src/emu_lcd.c`). Screens come out of the real `core/Src/st7565/st7565.c`. |
| BK4819 registers | A register file (`Src/emu_bk4819.c`). Scenes set the RSSI and raise squelch interrupts. |
| Keyboard, PTT | Scripted presses (`Src/emu_script.c`) |
| Delays, 10 ms tick | A simulated clock (`Src/emu_clock.c`) |
| Speaker path | A flag, so scenes can check the squelch gate |
| Factory calibration | The calibration block of a real V1 (`Src/emu_calib_data.c`) |

## Requirements

[w64devkit](https://github.com/skeeto/w64devkit), unpacked to `C:\w64devkit`
(override with `make W64DEVKIT=...`). The Makefile calls its gcc directly.

## Use

```bash
make            # build build/scout-emu.exe
make test       # scenes/tests.txt: firmware logic checks, fails on any failed expect
make screens    # scenes/manual.txt -> docs/manual/img/screens/*.png
make check      # re-render and compare byte for byte with the committed screens
```

Run `make check` after a firmware change to see which manual screens changed.
Run `make screens`, look at the PNGs, and commit them together with the change.

## Scenes

One scene per line, `name: cmd; cmd; ...`. Each scene boots the firmware like
V1's `Main()` in a fresh process, then runs its commands on the simulated
clock.

| Command | Effect |
|---|---|
| `wait <ms>` | Let the firmware run |
| `press <KEY>` / `hold <KEY>` | Short press (60 ms) / long press (700 ms), then 150 ms idle |
| `rssi <n>` | The BK4819 RSSI register reads `<n>` |
| `signal <hz> <rssi>` | A carrier: RSSI reads `<rssi>` when tuned within 6.25 kHz of `<hz>` (the scanner finds it) |
| `sql open` / `sql close` | A carrier comes on / goes off the air: raises `SQUELCH_LOST` / `SQUELCH_FOUND` (only if enabled in `REG_3F`). After an RX restart (`BK4819_RX_TurnOn`) the chip re-raises `SQUELCH_LOST` while the carrier is on. |
| `calib blank` | The factory squelch tables read as erased (`0xFF`). Put it first. |
| `count reset` | Zero the RX-restart counter that `expect restarts <n>` checks |
| `snap` | Write `<outdir>/<scene>.png` |
| `expect ...` | `audio on/off` (speaker path), `af mute/fm/am/alam` (BK4819 AF output), `freq <hz>`, `mod FM/AM`, `df on/off`, `gain <n>`, `sql <n>`, `reg <hex> <hex>` |

Keys: `0`-`9` `MENU` `UP` `DOWN` `EXIT` `STAR` `F` `PTT` `SIDE1` `SIDE2`.

The PNGs are 528×272: 4× scale with an 8 px margin, 3-colour palette, and no
timestamps, so the same firmware always gives the same bytes.
