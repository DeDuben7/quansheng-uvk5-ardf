# quansheng-uvk5-ardf

ARDF / fox-hunt receiver firmware for the Quansheng UV-K5 (hardware versions
V1 and V3) and the UV-K1, written from scratch for the Agger Martini Groep
scouting fox hunts. It turns the radio into a simple direction-finding
receiver: a squelch menu you can set to the signal strength you want to
chase, a DF tone for bearing hunts, a band scanner to find the fox, and a
one-click Windows updater (ScoutFlasher) so that anyone can put the firmware
on a radio without a toolchain.

## Features

- **Squelch menu, 0-9.** One setting for how strong a signal has to be before
  the radio opens. Lower means a more sensitive receiver.
- **DF tone (peiltoon).** The tone pitch follows the signal strength, so you
  can find the bearing of a fox by ear.
- **2 m and 70 cm.** Switch between the two amateur bands in the menu; each
  band remembers its last frequency. FM and AM.
- **Scanner.** Sweeps the fox-hunt part of the band (144-146 MHz or
  433-435 MHz) and lists the strongest signals.
- **Info screen with a live squelch page.** Shows the firmware version and the
  git commit it was built from (for example `v1.1 8b8060a`), the factory
  calibration, and a live readout of the squelch (RSSI, noise, glitch and the
  thresholds in use) for measuring with a signal generator.
- **PIN-locked bench TX.** Transmitting is locked at every power-on. After a
  PIN it works on the 2 m and 70 cm amateur bands only, at very low power,
  with a 60 s timeout. Meant for testing on the bench.

## Supported hardware

The UV-K5 ships in three hardware revisions built around **different
microcontrollers**. This is one repo with the MCU-agnostic code factored into
`core/` and each hardware revision supplying its own `platform/<target>/`
layer.

| Folder | Radio | MCU | Core | Status |
|--------|-------|-----|------|--------|
| [`platform/uv-k5-v1-dp32g030/`](platform/uv-k5-v1-dp32g030/) | UV-K5 **V1** | Nyquest DP32G030 | Cortex-M0 | working / flashed |
| [`platform/uv-k5-v2-py32f030/`](platform/uv-k5-v2-py32f030/) | UV-K5 **V2** | Puya PY32F030 | Cortex-M0+ | placeholder, not started |
| [`platform/uv-k5-v3-py32f071/`](platform/uv-k5-v3-py32f071/) | UV-K5 **V3** / UV-K1 | Puya PY32F071 | Cortex-M0+ | working on hardware |
| [`platform/host-emulator/`](platform/host-emulator/) | — (your PC) | x86-64, w64devkit gcc | — | display emulator + logic tests |

## Download

For the radio itself you only need Windows 11, the programming cable and an
internet connection:

- **[ScoutFlasher.exe](https://github.com/DeDuben7/quansheng-uvk5-ardf/releases/latest/download/ScoutFlasher.exe)**:
  the one-click updater. It recognises a V1 or V3 radio, installs the cable
  driver if needed, and flashes the firmware.
- **[Handleiding-Scout-porto.pdf](https://github.com/DeDuben7/quansheng-uvk5-ardf/releases/latest/download/Handleiding-Scout-porto.pdf)**:
  the manual, in Dutch.

ScoutFlasher always fetches the newest firmware from the latest GitHub
release and refuses to flash without an internet connection. That prevents an
old copy of the program from downgrading a radio by accident.

The first time you start it, Windows SmartScreen may warn about an unknown
publisher. Click "Meer info", then "Toch uitvoeren".

All releases, with their change notes, are on the
[releases page](https://github.com/DeDuben7/quansheng-uvk5-ardf/releases);
the history is in [`CHANGELOG.md`](CHANGELOG.md).

Every firmware image carries the git commit it was built from. The Info
screen (page 1) shows it next to the version, for example `v1.1 8b8060a`, and
the binary contains the marker `SCOUT-VERSION:<v>+<commit>`. Quote this when
reporting a problem.

## Layout

```
core/                    MCU-agnostic code, shared by every platform
  Inc/, Src/             drivers (bk4819, st7565, battery, radio, audio_df),
                         the scout app (scout/), UI primitives (ui/)
  external/printf/       vendored printf
platform/<target>/       one folder per hardware revision
  Inc/, Src/             that platform's HAL implementation (peripherals/, bk4819_hw.c, ...)
  Makefile               builds core + platform into build/core/ and build/plat/
tools/scout-flasher/     ScoutFlasher, the Windows updater
tools/release.ps1        builds and publishes a release
docs/manual/             source of the Dutch manual
```

See `core/README.md` for the rules on what may live in `core/` and the
platform HAL contract it depends on.

### Header-path rule

Both `core/Inc` and `platform/<target>/Inc` are on every platform's include
path. A header that is **byte-identical across platforms** (e.g.
`bk4819_regs.h`, `st7565.h`, `battery.h`) lives in `core/Inc`. A header whose
content genuinely differs per platform (e.g. `peripherals/spi_lcd.h`, whose
V1 version documents hardware-NSS chip-select and a real reset line, while
the V3 version documents a manual GPIO chip-select and no reset line) lives
in `platform/<target>/Inc` under the same relative path. This is why shared
`.c` files in `core/Src` need no per-platform `#ifdef`s or edits: `#include
"peripherals/spi_lcd.h"` resolves to whichever version is on that platform's
include path.

### Core is compiled per platform

There is no prebuilt `core.a`. Each platform's Makefile compiles the `core/`
sources itself, with its own compiler flags, into that platform's own
`build/` tree (`build/core/...` for shared sources, `build/plat/...` for that
platform's own sources) — V1 with `-mcpu=cortex-m0`, V3 with
`-mcpu=cortex-m0plus`. Because of this, code in `core/` must stay
**ISA-neutral**: no Cortex-M0+-only instructions/intrinsics, and no
reference to any platform's registers or peripherals. See `core/README.md`.

Each Makefile's `OBJS` list order is deliberate (link order affects code
addresses) — don't reshuffle it casually.

## Build

```bash
cd platform/<target> && make
```

V3 also has `make flash` (its own ProtocolV2 flasher under
`tools/k5v3-flasher/`, `BLVER=7.00` — see `platform/uv-k5-v3-py32f071/README.md`).
V1 flashes with `K5TOOL.exe -wrflash firmware_uvk5_v1.packed.bin` (see
`platform/uv-k5-v1-dp32g030/README.md`).

`make clean` is cmd.exe-only in both Makefiles (it does nothing under Git
Bash) — use `rm -rf build` instead if you're not in cmd.exe.

## Host emulator

`platform/host-emulator/` runs the real `core/` firmware on your PC with
w64devkit gcc (unpacked to `C:\w64devkit`). It renders the 128x64 display
and runs the logic tests, so most of the UI can be developed and tested
without a radio:

```bash
cd platform/host-emulator
make            # build build/scout-emu.exe
make test       # firmware logic checks, fails on any failed expect
make screens    # render the manual's screen images
make check      # re-render and compare with the committed screens
```

See `platform/host-emulator/README.md` for details.

## Reference firmware

The ports were made against these upstream projects, used as read-only
references (they are not part of this repo):

- [reald/uv-k5-firmware-custom](https://github.com/reald/uv-k5-firmware-custom):
  the ARDF fork, DP32G030 (V1 reference), Apache-2.0.
- [armel/uv-k1-k5v3-firmware-custom](https://github.com/armel/uv-k1-k5v3-firmware-custom):
  the UV-K1 / UV-K5 V3 port, PY32F071 (V3 reference), Apache-2.0.

Their lineage goes back to
[DualTachyon/uv-k5-firmware](https://github.com/DualTachyon/uv-k5-firmware)
and [egzumer/uv-k5-firmware-custom](https://github.com/egzumer/uv-k5-firmware-custom).
See [`NOTICE`](NOTICE) for the credits.

## Toolchain

`arm-none-eabi-gcc` 10.2.1, GNU Make, Python 3.10+ with `crcmod`.

## Release (firmware + flasher + manual)

Every firmware given to anyone gets a version. To make a release:

1. Bump `SCOUT_VERSION` in `core/Inc/scout/version.h`. If the flasher
   changed too, bump `FLASHER_VERSION` in `tools/scout-flasher/sf/__init__.py`.
2. Add a `## X.Y` section to `CHANGELOG.md`. Its text becomes the release
   notes on GitHub.
3. Commit, then run:

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\release.ps1 -Version X.Y -Publish
   ```

   `-Publish` needs the GitHub CLI (`winget install GitHub.cli`, then
   `gh auth login`), a clean working tree, the `main` branch and push rights
   to `origin` (tried up front with a dry-run push that uses the `gh`
   login), and asks you to type `yes`. Without `-Publish` the script only builds, into
   `release/X.Y/`, so you can check the result first. For a test release
   use a suffix and `-Prerelease` (`-Version 1.1-rc1 -Publish -Prerelease`):
   a pre-release never becomes *Latest*, so ScoutFlasher ignores it.

The script exports the cable driver from the build PC, builds V1 and V3,
checks that both binaries carry `SCOUT-VERSION:<v>+<commit>`, runs the
emulator tests, checks that the manual screens are current, renders the
manual, runs the flasher tests, and writes the release assets:
`ScoutFlasher.exe`, `Handleiding-Scout-porto.pdf`, and the two files
ScoutFlasher reads when it updates (`scout-firmware.zip` and
`scout-update.json`). With `-Publish` it also tags `vX.Y`, pushes, and
creates the GitHub release with the `## X.Y` notes from `CHANGELOG.md`.
`-MinFlasher` (default `1.1`) sets the oldest ScoutFlasher that may flash
the release; raise it only when the package format or the flashing changes.

**Rollback.** ScoutFlasher flashes whatever GitHub marks as *Latest*. To roll
back, mark an older release as *Latest* on its GitHub release page.

Developer tools: Git for Windows, arm-none-eabi-gcc + make, w64devkit
(`C:\w64devkit`, for the emulator), Python 3.10+ with pyserial, certifi,
pytest and PyInstaller, Microsoft Edge (to render the PDF), and the Prolific PL2303
driver 3.3.11.152 installed on the build PC (the script exports it into the
EXE at build time; it is not stored in the repo).

## Licence

Apache License 2.0, see [`LICENSE`](LICENSE). Third-party code, and the
programs bundled into ScoutFlasher.exe, are credited with their own licences
in [`NOTICE`](NOTICE).
