# scout — UV-K5 V3 / UV-K1 (Puya PY32F071)

**Status: port in progress** (branch `port/uv-k5-v3-py32f071`).

Port of the scout ARDF fox-hunt receiver firmware to the **Quansheng UV-K5
hardware version 3** (and the identical UV-K1), which uses the Puya
**PY32F071** microcontroller (Cortex-M0+) instead of the V1's Nyquest
DP32G030 (Cortex-M0).

## What changes vs V1

The MCU is a different silicon family, so the entire bottom layer is
re-implemented against the PY32F071 register map:

- **Rewritten for PY32:** `bsp/` register defs, all `peripherals/` drivers
  (gpio, spi, i2c, adc, systick, system/clock, eeprom/flash, keyboard,
  backlight), startup (`start.S`), linker script, clock/PLL init.
- **Carries over unchanged (MCU-agnostic):** the application (`scout/`,
  `audio_df`, `radio`), the external-chip drivers `bk4819` and `st7565`
  (they talk over SPI/I2C and don't care which MCU drives the bus), and
  the `ui/` primitives.

## Flashing (differs from V1)

The PY32 bootloader ("ProtocolV2", reports `7.00.07`) is different from V1 —
and **`K5TOOL.exe` cannot flash it** (see the Phase 0 note below). Flash with
the vendored local CLI flasher via the Makefile:

```bash
make flash                 # rebuild + flash, COM9, BLVER=7.00
make flash PORT=COM7       # different port
```

Requires Python 3 + `pyserial`. Enter bootloader mode the usual way: hold PTT
while powering on (white flashlight LED steady). The LED blinks while pages are
written; a full image takes ~8 s of programming (~10 s end to end) and should
report `0 resends`. The tool exits non-zero if programming fails. For a packet
trace, run the CLI directly with `--verbose` (see `tools/k5v3-flasher/README.md`).
If a flash fails midway the radio stays in the bootloader. Just re-flash.
Browser alternative that also works: UVTools2
(https://armel.github.io/uvtools2/, WebSerial).

## Reference

`../../armel-k5v3-src/` (armel/uv-k1-k5v3-firmware-custom) — the V3/PY32F071
reference port, analogous to `../../reald-src/` for V1. Read-only reference;
never flashed as-is.

## Phase 0 result

**SUCCESS (2026-08-21):** `firmware_uvk5_v3.bin` boots on a physical UV-K5 V3
and the flashlight LED (GPIOC13) blinks. Toolchain, startup, clock, GPIO, and
the flash pipeline are all proven.

### Flashing note — K5TOOL does NOT work for this V3

`K5TOOL.exe v1.8.0.0 -wrflashraw` reaches the bootloader (beacon `7.00.07`,
ProtocolV2) and unlocks flash, but its block write never completes: the radio
re-beacons instead of ACKing and the write times out. Tried version strings
`2.01.23` (rejected, "error code 1"), `*` and `7.00.07` (both time out on the
first block). Port/cable are healthy — this is a K5TOOL ProtocolV2 limitation.

**Working methods:**
1. **Local CLI (preferred): `make flash`** — a vendored copy of muzkr's
   ProtocolV2 `serialtool` (`tools/k5v3-flasher/`, MIT). The handshake
   BL-version must match the bootloader family (`7.00`, not a 7-char string
   or `?`) to unlock programming. Verified on hardware 2026-08-21 — flashed a
   modified blink rate and the LED rate changed.
   *2026-09-28:* the vendored tool used to stall at random pages. Its packet
   parser dropped any packet that arrived split across two serial reads,
   usually a page ACK. That is fixed, so there is no page padding and resends
   are rare. Verified with two consecutive full flashes: 55 pages in ~7.8 s,
   0 resends each.
2. **Browser: UVTools2** (https://armel.github.io/uvtools2/, WebSerial) — also
   flashes our local `.bin`.

## Phase 1 result (pending hardware)

`main.c` now brings up the LCD and renders a screen, on top of the Phase 0
flashlight-LED heartbeat blink:

```c
SPI_LCD_Init();
ST7565_Init();
UI_DisplayWelcome();   // draws the AMG bitmap + text and blits it itself
```

**To verify on hardware:** `make flash`, then power-cycle the radio.

- **Expect:** the AMG welcome screen (block letters "A M G" + "AGGER MARTINI
  GROEP" + a border) appears on the LCD immediately at boot, and the
  flashlight LED (GPIOC13) keeps blinking underneath it (heartbeat — proves
  the board isn't dead even if the screen is wrong/blank).
- **If the LED blinks but the LCD stays blank:**
  1. Check assumption **A1.1** —
     this board drives no LCD RESET line, so `ST7565_HardwareReset()` is a
     no-op. If the panel needs an actual reset pulse to come out of
     power-on in a sane state, this is the first suspect.
  2. Check the contrast byte — `ST7565_Init()` sets EV (electronic volume /
     contrast) to `31` (`Src/st7565/st7565.c`, the `cmds[]` table after
     `ST7565_CMD_SET_EV`). Too low/high can look like a blank/all-black
     screen.
  3. Check SPI mode/pins — `SCK=PA5`, `MOSI=PA7`, `CS=PB2` (manual,
     active-low, NSS=SOFT), `A0=PA6` (`Inc/peripherals/spi_lcd.h`). Confirm
     these against the actual V3 board with a meter/scope if the screen
     stays blank after (1) and (2) check out.

## Phase 2 result (pending hardware)

`main.c` now brings up RX on the BK4819 and shows a live RSSI reading on the
LCD, on top of the Phase 0 heartbeat blink and Phase 1 LCD bring-up (both
kept unchanged):

```c
BK4819_Init();
radio_init(&radio_cfg);   // 145.500 MHz, FM, narrow — same idiom as V1's Main()
// ... draws "145.5000" + "FM RX" once, then every 10 heartbeat blinks:
//     read radio_get_rssi(), format "RSSI:NNN" (hand-rolled, no sprintf), blit
```

`radio_init()` (`Src/radio/radio.c`, carried over unchanged from V1) performs
the full V1 sequence in the V1 order: `BK4819_RX_TurnOn()`, asserts RX_ENABLE
via `BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, true)`, tunes the PLL
(Hz -> BK4819's native 10 Hz units), sets filter bandwidth + gain,
`BK4819_SetAF(BK4819_AF_FM)`, then `AUDIO_AudioPathOn()` +
`BK4819_ExitTxMute()`.

**To verify on hardware:** `make flash`, then power-cycle the radio.

- **Expect:** after the ~1.5 s welcome screen, the LCD shows `145.5000` +
  `FM RX` and a `RSSI:NNN` line that updates a few times a second and moves
  when you wave a hand near the antenna or bring a 2 m signal source close;
  the flashlight LED keeps blinking underneath throughout. On a strong 2 m
  carrier you should also hear RX audio.
- **If RSSI is static or garbage:** suspect the BK4819 bit-bang bus —
  transport timing (assumption **A2.7**: NOP-count delay transcribed from armel's
  `bk4829.c`, unverified against the real PY32 core clock) or the bus pins
  themselves (**A2.1**: CS=GPIOF9, SCL=GPIOB8, SDA=GPIOB9). A stuck/garbage
  reading usually means reads aren't actually round-tripping the chip.
- **If there's no audio** (but RSSI moves, so the SPI/I2C-ish bus itself is
  fine): check `BK4819_SetAF`/RX_ENABLE actually took, the audio-path GPIO
  (V3 pin GPIOA8, `Src/peripherals/audio_path.c`) is driving high, and that
  squelch isn't closed — `radio_init()` opens it fully
  (`BK4819_SetupSquelch(0,0,0,0,255,255)`), so a persistent mute points at
  AF/RX_ENABLE/audio-path wiring rather than squelch logic.

## Phase 3 result (pending hardware)

`main.c` now also brings up the battery-voltage ADC and the keyboard matrix,
and shows a live battery voltage/percent line and a "last key pressed" line
on the LCD, on top of the Phase 0 heartbeat blink, Phase 1 LCD bring-up, and
Phase 2 RSSI line (all kept unchanged):

```c
ADC_Init();
battery_init(&battery_cfg);   // read_adc = v3_read_battery_adc (wraps ADC_GetBatteryRaw)
// ... every loop pass: battery_task(), KEYBOARD_Poll() (remember last non-INVALID key)
// ... every 10 heartbeat blinks: format "BATT:V.TV PPP%" and "KEY:NN" (hand-rolled, no
//     sprintf), alongside the existing RSSI line, then blit
```

`battery_init()`'s wiring mirrors V1's `Src/main.c` exactly (a
`battery_config_t` with a `read_adc` callback), except V3 has no persistent
storage (assumption **A3.1**) to read a per-radio ADC calibration word from
like V1 does (EEPROM `0x1F40`+word[3]) — `adc_760` is passed as `0`, so
`battery_init()` always falls back to its hardcoded `BATTERY_CAL_760` nominal
(**A3.2**, `Src/battery/battery.c`). Battery percent is therefore
**approximate** until a real per-radio calibration word/value is measured and
wired in.

**To verify on hardware:** `make flash`, then power-cycle the radio and press
some keys.

- **Expect:** after the welcome screen and RX screen, the LCD shows the
  existing `RSSI:NNN` line plus a `BATT:V.TV PPP%` line reading roughly
  **7.0–8.4 V** (a healthy 2S Li-ion pack) and a `KEY:--` line that changes to
  `KEY:NN` (the numeric `KEY_Code_t` value, 0–18) when you press a key on the
  keypad, updating a few times a second like the RSSI line.
- **If the KEY line never changes / shows the wrong key:** suspect the
  keyboard matrix pins or debounce timing (assumptions **A3.4**/**A3.7**) —
  column/row pin assignment
  (`Src/peripherals/keyboard.c`: cols PB3-6, rows PB12-15) or the transcribed
  armel debounce sequence (8 samples, 10 us apart, 2 consecutive matches).
- **If the battery reading is absurd** (0 V, pegged at max, or wildly off from
  a multimeter reading on the pack): suspect the ADC channel/scaling/
  calibration (assumptions **A3.2**/**A3.5**/**A3.6**) — the PY32 ADC1
  channel-8/PB1 wiring and self-calibration sequence (`Src/peripherals/
  adc.c`), or the hardcoded `BATTERY_CAL_760` nominal not matching this
  radio's actual divider tolerance.
- Remember: battery percent is approximate (hardcoded calibration, no
  per-radio factory value — **A3.2**), and there is **no persistent storage**
  on this port (**A3.1**), so nothing measured here survives a power cycle.

## Phase 4 result (pending hardware) — FULL SCOUT APP

**Port complete.** `main.c` now drops all Phase 1-3 test scaffolding (the
flashlight heartbeat blink and the ad-hoc RSSI/BATT/KEY status lines) and
boots straight into the real scout application, reproducing V1's
`Src/main.c` boot order exactly:

```c
SCHEDULER_Init();              // SysTick 10 ms tick
ADC_Init();                    // battery ADC front end

SPI_LCD_Init();
ST7565_Init();
BK4819_Init();
BACKLIGHT_On();

UI_DisplayWelcome();

radio_init(&radio_cfg);        // 145.500 MHz, FM, narrow
battery_init(&battery_cfg);    // adc_760 = 0 -> BATTERY_CAL_760 fallback (A3.1/A3.2)
audio_df_init(&df_cfg);        // rssi_floor=40, rssi_ceil=200, tone_gain=96

DelayMs(1500);                 // keep welcome on-screen, same as V1
scout_app_run();                // hands off forever — same entry point V1 uses
```

`scout_app_run()` (`Src/scout/app.c`, carried over byte-identical from V1)
owns the application loop from here: keyboard dispatch, the smoothed-RSSI
feed to audio-DF, `battery_task()`, the scanner, and rendering the home
screen (`Src/scout/home.c`) at ~10 Hz. It never returns.

**To verify on hardware:** `make flash`, then power-cycle the radio.

- **Expect:** the AMG welcome screen for ~1.5 s, then the scout home screen —
  tuned frequency, FM/AM modulation, an RSSI bar, and the battery percent —
  all live and updating.

### Key map (from `Src/scout/app.c` / `Src/scout/home.c`, verbatim from V1)

| Key | Short press | Long press (~500 ms) |
|---|---|---|
| `0`-`9` | Enter a frequency digit; auto-commits (tunes) once 6 digits are buffered | — |
| `EXIT` | Clear the in-progress frequency-entry buffer | — |
| `UP` | Increase front-end gain one step (`radio_gain_incr`) | — |
| `DOWN` | Decrease front-end gain one step; past the minimum this engages "mistune" deliberate detuning (`radio_gain_decr`) | — |
| `SIDE1` | Toggle the audio-DF tone (enable/disable); disabling restores normal RX audio routing | — |
| `SIDE2` | Toggle AM/FM modulation | Swap between the two band presets (2 m / 70 cm), remembering each band's last-tuned frequency |
| `MENU` | — | Enter the scanner (disabled while a frequency entry is in progress) |

While the scanner screen is active, `EXIT` aborts and restores the prior
radio state, `*` re-runs the sweep, digits `1`-`5` tune to that ranked
candidate, and `UP`/`DOWN` still adjust gain if the scanner itself doesn't
consume the key (kept reachable so a "SAT! LOWER GAIN" hint stays
actionable mid-scan).

### On-hardware calibration TODO

- **Audio-DF floor/ceiling + gain table (A4.6/A2.6):** `rssi_floor=40`,
  `rssi_ceil=200`, `tone_gain=96` are carried over unverified from V1;
  confirm the tone's low/high pitch lands on a sensible RSSI range on this
  board and that `tone_gain` isn't clipping/too quiet. Cross-check against
  the manual gain table (`Src/radio/radio.c`, `RADIO_GAIN_MAX`/
  `radio_get_gain_db()`) used for the "SAT! LOWER GAIN" mistune path.
- **SysTick timing sanity (A4.2):** confirm a real ~1 s Side2 long-press
  actually reads as `LONG_PRESS_TICKS = 50` (500 ms) worth of
  `scout_tick_10ms()` — i.e. that the SysTick 10 ms tick (`Src/scheduler.c`)
  is genuinely 10 ms on this board's real HSI clock, not systematically
  fast/slow.
- **Backlight (A4.4):** `BACKLIGHT_On()` is a fixed on/off GPIO toggle on
  this port (no PWM brightness or auto-off timer, unlike V1's
  `backlight_config_t`) — confirm the backlight pin/polarity is right and
  decide if an auto-off timeout is worth porting later.
- **Persistent storage / band presets absent (A3.1):** the 2 m/70 cm band
  presets in `Src/scout/app.c` and the battery ADC calibration word both
  live in RAM only — there is no EEPROM/flash port on V3, so none of it
  survives a power cycle. A real per-radio battery calibration value
  (A3.2) and any persisted band presets would need a new storage backend,
  out of scope for this port.
