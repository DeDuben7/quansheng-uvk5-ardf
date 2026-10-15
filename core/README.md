# core/ — MCU-agnostic shared code

Everything in `core/` is compiled once per platform (into that platform's own
`build/core/` tree, with that platform's own compiler flags — see the root
`README.md`). Nothing here is prebuilt into a library and shared as a binary;
the source is shared, the object code is not.

## What belongs here

Only code that has no dependency on which microcontroller it runs on:

- **No platform registers.** Code in `core/` must never read or write an MCU
  peripheral register directly, and must never `#include` a
  platform-specific header from `platform/<target>/Inc` by name. It talks to
  hardware only through the HAL contract below.
- **No ISA-specific instructions or intrinsics.** Core is compiled for both
  Cortex-M0 (V1, `-mcpu=cortex-m0`) and Cortex-M0+ (V3,
  `-mcpu=cortex-m0plus`). Anything that only exists on M0+ (e.g. hardware
  single-cycle multiply intrinsics, M0+-only instructions) is off limits —
  code here must build and run correctly on the plain M0.
- **No `driver.def`/BSP register-definition dependencies.** Those are
  generated per platform from `hardware/*/*.def` and live in
  `platform/<target>/bsp/`.

What's currently here: the BK4819 driver (`Src/bk4819/bk4819.c`), the ST7565
LCD driver (`Src/st7565/st7565.c`), the battery module
(`Src/battery/battery.c`), factory calibration decoding (`Src/calib/calib.c`:
battery, TX bias, squelch tables), the radio state machine
(`Src/radio/radio.c`, including the squelch and its audio gate), audio
direction-finding (`Src/audio_df/audio_df.c`), the scout application
(`Src/scout/`: `app.c` screens + keys, `home.c`, `menu.c` settings menu,
`scan.c`, `info.c` factory-cal + live squelch pages, `gaincal.c`, `tx.c`,
`version.c`), the UI primitives (`Src/ui/`), and the vendored
`external/printf/`.

Release settings live in two headers: `Inc/scout/version.h` (`SCOUT_VERSION`,
bump it for every release) and `Inc/scout/config.h` (power-on defaults:
144.450 MHz, FM, narrow, squelch 3).

`platform/host-emulator/` compiles all of this for the PC against fake
hardware: `make test` checks the app logic, `make screens` renders the
manual's screenshots. Run it after any change here.

Note: `Src/ui/inputbox.c` / `Inc/ui/inputbox.h` exist in `core/` but are not
currently referenced by either platform Makefile's `OBJS` list — they are not
part of either build yet.

## The platform HAL contract

Everything below is implemented once per `platform/<target>/`, with a
matching header either in `core/Inc` (if identical across platforms) or
`platform/<target>/Inc` (if it differs — see the root README's header-path
rule). Core code calls these by name; it never implements them.

| Contract | Declared in | Notes |
|---|---|---|
| `SYSTEM_DelayMs` (V1) / `DelayMs`, `DelayUs` (V3) | platform `Inc/peripherals/system.h` or `delay.h` | Busy-wait delay primitives. Naming differs per platform today — not yet unified. |
| `scout_tick_10ms()` | `core/Inc/peripherals/scheduler.h` | Free-running 10 ms tick, driven by each platform's own SysTick setup; each platform implements it in its own `Src/scheduler.c`. |
| `SPI_LCD_Init`, `SPI_LCD_WriteByte`, `LCD_CS_Low`/`LCD_CS_High`, `LCD_A0_Command`/`LCD_A0_Data` | platform `Inc/peripherals/spi_lcd.h` (differs per platform — see root README) | LCD bus + control-line HAL used by `core/Src/st7565/st7565.c`. |
| `ST7565_HardwareReset` | `core/Inc/st7565/st7565.h`, implemented in platform `Src/peripherals/spi_lcd.c` | Part of the LCD HAL; V1 pulses a real reset line, V3 is a no-op (no reset line on that board). |
| `BK4819_ReadRegister`, `BK4819_WriteRegister`, `BK4819_Init`, `BK4819_RX_TurnOn` | `core/Inc/bk4819/bk4819_hw.h` | Bus bit-banging + chip bring-up differ per platform; everything else about the BK4819 (tune, RSSI, AF mux, filters, squelch, GPIO, tones) is shared in `core/Src/bk4819/bk4819.c`. `gBK4819_GpioOutState` is declared `extern` here and defined in `bk4819.c` — written by platform `BK4819_Init()`, read/modified by core's `BK4819_ToggleGpioOut()`. |
| `AUDIO_AudioPathOn`, `AUDIO_AudioPathOff` | platform `Inc/peripherals/audio_path.h` | V1 defines these `static inline`; V3 has a `Src/peripherals/audio_path.c`. |
| `KEYBOARD_Poll()` | `core/Inc/peripherals/keyboard.h` | Header is shared; each platform implements the scan in its own `Src/peripherals/keyboard.c`. |
| `backlight_hal_set(bool on)` | `core/Inc/peripherals/backlight_hal.h`, implemented in platform `Src/peripherals/backlight.c` | The only backlight call core makes; `core/Src/scout/light.c` decides when (menu item Verlichting: auto / aan / uit). Each platform keeps its own driver API in its own `Inc/peripherals/backlight.h`: V3 exposes `BACKLIGHT_On`/`BACKLIGHT_Off`, V1 a snake_case PWM API (`backlight_init`, `backlight_turn_on`, `backlight_turn_off`, ...). V1's own auto-off timer (`timeout_500ms`) stays 0: the timing is in core. |
| Battery raw-ADC callback + `adc_760` calibration | `core/Inc/battery/battery.h` (`battery_config_t`) | The platform passes a `read_adc(uint16_t *voltage, uint16_t *current)` callback and the radio's factory `adc_760` calibration word (ADC count at 7.60 V, read from EEPROM) into `battery_init()`. `core/Src/battery/battery.c` falls back to `ADC_760_FALLBACK` (1900) if `adc_760` is 0. |

Core code accesses all of the above only through these names — if a change to
`core/` needs anything else from the platform, that's a sign the new
functionality needs its own HAL entry, not a direct register poke.
