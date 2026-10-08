# scout-v2 refactor plan (Foxxer-style)

Goal: a codebase where every `.c` file compiles in isolation against its own
header. No `gEeprom`, no application-level globals reached from drivers.
Each module owns its config and state.

Reference style: `C:\git\FoxxerSTM32CubeIde` (sa818, st7735, attenuator, etc.)

## Target directory layout

```
scout-v2/
├── Inc/                  # public headers, one subdir per module
│   ├── bk4819/
│   │   └── bk4819.h
│   ├── st7565/
│   │   └── st7565.h
│   ├── backlight/
│   │   └── backlight.h
│   ├── keyboard/
│   │   └── keyboard.h
│   ├── adc/
│   │   └── adc.h
│   ├── eeprom/
│   │   └── eeprom.h
│   ├── ui/
│   │   ├── helper.h
│   │   ├── font.h
│   │   └── welcome.h
│   ├── audio_df/         # application: audio-DF tone module
│   │   └── audio_df.h
│   ├── radio/            # application: tuning + AGC + freq presets
│   │   └── radio.h
│   ├── settings/         # application: EEPROM persistence
│   │   └── settings.h
│   └── board.h           # wires modules together — only file that knows pinout
├── Src/
│   ├── bk4819/bk4819.c
│   ├── st7565/st7565.c
│   ├── ...
│   ├── board.c
│   └── main.c
├── bsp/dp32g030/         # register definitions, untouched
├── external/             # CMSIS, printf
├── docs/                 # datasheets
├── Makefile
├── start.S
├── init.c
└── firmware.ld
```

## Driver contract

Every driver `.c` file:

1. **Has zero `extern` references to application-level globals.**
2. Defines its own state in `module-static` variables inside the `.c` file
   (`static` at file scope), never exposed.
3. Receives config via an `_init(const cfg_t* cfg)` call. Caller (board.c)
   provides the config struct.
4. Exposes a clean API:
   - `module_init(const module_config_t*)` — set up
   - `module_task(uint32_t tick_10ms)` — optional periodic work
   - Getters / setters for runtime values
5. Hardware dependencies (pins, peripheral handles) come **either** via the
   config struct **or** via function pointers in the config struct (Foxxer's
   `ST7735_Init_Func` pattern). Pin numbers themselves stay in `board.h`.

## Migration order (small → big)

Each step ends with a successful build.

### Step 1 — Skeleton restructure (no code changes)
- Move `driver/*` → `Src/<module>/`, headers → `Inc/<module>/`.
- Move `ui/helper.c`, `ui/welcome.c`, `font.c`, `bitmaps.c` → matching `Inc/ui/`, `Src/ui/`.
- Update `Makefile` `OBJS =` and `INC =` paths.
- Repair every `#include` path.
- Goal: same firmware behaviour, just rearranged. Build clean.

**Risk**: ~1 hour, mostly mechanical.

### Step 2 — Refactor `backlight`
- Define `backlight_config_t { uint8_t timeout_500ms; uint8_t min_brightness; uint8_t max_brightness; bool min_always_on; }`.
- `backlight_init(const backlight_config_t* cfg)` stores cfg in module-static.
- Remove `gEeprom` reference; use stored cfg.
- Move `gBacklightCountdown_500ms` into the module (`static`).
- New API: `backlight_turn_on()`, `backlight_turn_off()`, `backlight_task_500ms()`.
- `board.c` constructs the config and calls `backlight_init`.

**Risk**: 30 min. Backlight is small.

### Step 3 — Refactor `bk4819`
- Big one. Currently 1800+ lines, references `gEeprom` in two places and pulls in a chain.
- Define `bk4819_config_t` (just the fields the chip needs) plus a context object for state (gain, current AGC mode, AM-fix counters etc.) that's module-static.
- Audit every `gEeprom.X` and `g<Other>` reference; either inline as `static` or pass via config.
- Public API:
  - `bk4819_init(const bk4819_config_t*)`
  - `bk4819_set_frequency(uint32_t hz)`
  - `bk4819_set_modulation(bk4819_mod_t)`
  - `bk4819_set_bandwidth(bk4819_bw_t)`
  - `bk4819_set_squelch(uint8_t level)`
  - `bk4819_set_af_mode(bk4819_af_t)`  // ALAM, MUTE, FM, etc.
  - `bk4819_tone1_set(uint16_t hz, uint8_t gain)`
  - `bk4819_tone1_disable(void)`
  - `bk4819_get_rssi(void)`
  - `bk4819_get_rssi_dbm(int8_t band_corr_db)`  // band corr passed in, not looked up

**Risk**: 2–3 hours. The big payoff: audio-DF becomes a small `audio_df` module that just calls `bk4819_tone1_set(hz, 96)` and `bk4819_set_af_mode(AF_ALAM)`. No SetAF intercept lock, no RX_TurnOn guard, no fighting.

### Step 4 — Refactor `st7565`
- Most of `st7565.c` is fine. Mainly removing the `extern uint8_t gFrameBuffer` style and moving the framebuffer ownership to the display module.
- API: `st7565_init()`, `st7565_clear()`, `st7565_blit(line)`, `st7565_blit_status_line()`, `st7565_get_framebuffer()` returns a `uint8_t (*)[128]`.

**Risk**: 1 hour.

### Step 5 — Refactor `keyboard` + `adc`
- Both fairly clean already. Mostly path renames.

**Risk**: 15 min each.

### Step 6 — Refactor `helper/battery`, `helper/boot`
- Battery reads ADC, returns voltage / percent. No `gEeprom` after we move battery-type config into its own `battery_init(battery_config_t)`.
- Boot mode detection takes pin state, returns enum. No globals needed.

**Risk**: 30 min each.

### Step 7 — Application modules (`audio_df`, `radio`, `settings`)
- These are *new* code; we just have to write them. They consume the refactored drivers cleanly.

**Risk**: ~3–4 hours total. The actual application work.

## Open questions

1. **Build system**: stay on Make, or switch to CMake like Foxxer? CMake gives us a real `compile_commands.json` for clangd / IntelliSense. But it's another switch on top of everything else. Recommend: **stay on Make for now**, switch later if needed. The Makefile is short anyway.
2. **C version**: stay on `-std=c2x` (current). Foxxer uses C11; either works.
3. **Config-via-struct vs config-via-function-pointers**: Foxxer uses function pointers for `Init`, `WriteReg`, etc. so the driver doesn't know if it's on SPI / I2C / mock. We don't need that flexibility — the BK4819 is always on the same bus. Keep it simple: plain struct fields, not callbacks, unless we need test mocks.
4. **Should drivers report errors via `<module>_status_t` enums?** Foxxer does. Worth adopting since we're refactoring anyway — makes failure paths explicit.

## What I am NOT going to do tonight

This refactor is multi-hour. Half-doing it would leave broken files. Better
to start fresh with a clear head in the next session.

Tonight ends with: `scout-v2` initialised, skeleton committed, datasheets in
place, this refactor plan written. Pick up at Step 1 next time.
