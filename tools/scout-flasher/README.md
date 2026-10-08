# ScoutFlasher

ScoutFlasher is a one-click Windows 11 updater for the scout portos, for
both UV-K5 V1 and V3. The user only needs the EXE, an internet connection
and the programming cable. The EXE has its own Python runtime and the cable
driver. It carries no firmware: at every start it downloads the latest
GitHub release (images, manual) and checks it (`sf/update.py`). It refuses
to flash when it is offline, or when the EXE is older than the release
allows (`min_flasher`), so nobody flashes an old version by accident.

Build a release with `tools/release.ps1` (see the root README). It exports
the cable driver from the build PC into `driver/`, writes the build's commit
into `sf/_build_info.py` and a Windows version resource into
`build/version_info.txt`, and runs PyInstaller with `scout-flasher.spec`.
`tools/make_release_assets.py` then writes the four fixed-name GitHub release
assets into `release/<version>/`: `ScoutFlasher.exe`, `scout-firmware.zip`
(manifest, both images, manual, LICENSE, NOTICE), `scout-update.json` (what the EXE reads
first) and `Handleiding-Scout-porto.pdf`. With `-Publish` the script tags
and pushes the release and creates it on GitHub with the `gh` CLI.

## What it does

1. **Cable.** It finds the USB-serial cable through PowerShell's PnP
   cmdlets. The scout cable is a Prolific PL2303 `REV_0300`. That chip
   needs driver **3.3.11.152** (2010); the 2024 driver from Windows Update
   rejects it with "PHASED OUT", Code 10. If the wrong driver is active,
   ScoutFlasher restarts itself as administrator (one UAC prompt) with
   `--fix-driver`. It then stages `driver/pl2303-3.3.11.152` with `pnputil`
   and forces it with `UpdateDriverForPlugAndPlayDevices(INSTALLFLAG_FORCE)`.
   This check runs on every start.
2. **Radio.** In bootloader mode the radio repeats a device-info beacon.
   The vendored k5v3-flasher `msg.py` decodes it. Bootloader `2.x` means V1
   and `7.x` means V3. For any other version it stops without flashing.
3. **Flash.** Both flashers have already been used on these radios:
   - **V3:** the vendored `platform/uv-k5-v3-py32f071/tools/k5v3-flasher`,
     imported unchanged.
   - **V1:** the bundled `bin/K5TOOL.exe -wrflash` (GPL-3.0, unmodified;
     see `bin/README.txt`). It runs in a temp folder.
4. **Log.** Every run writes `ScoutFlasher-log-<date>-<time>.txt` on the
   desktop.

## Develop

```bash
python -m pytest tools/scout-flasher/tests -q
python tools/scout-flasher/scout_flasher.py --simulate ok
```

Simulate scenarios: `ok`, `no-cable`, `driver-fix`, `driver-stuck`, `no-radio`,
`unknown-bl`, `flash-fail`, plus the download problems `offline`,
`needs-new-exe`, `newer-exe` and `bad-download`. They need no hardware, no
network and show no UAC prompt. Their logs go to `%TEMP%`.

To try the real download against a local fake release, serve a folder with
`scout-update.json` and `scout-firmware.zip` (`python -m http.server`) and
start the wizard with `SCOUTFLASHER_UPDATE_URL=http://127.0.0.1:<port>/`.

| Path | Purpose |
|---|---|
| `scout_flasher.py` | Entry point: wizard, `--simulate`, `--fix-driver` |
| `sf/ui.py` | The Dutch wizard (Tkinter) |
| `sf/backend.py` | Real and simulated steps behind one interface (incl. fetching the release) |
| `sf/update.py` | Downloads and verifies the latest release (`UpdateError`: offline, too_old, bad_info, bad_download) |
| `sf/driver.py` | Cable query and driver decision; elevated driver fix |
| `sf/radio.py` | COM port and bootloader beacon |
| `sf/flash.py` | V3 and V1 flashing, with progress |
| `sf/manifest.py`, `sf/log.py` | Package checksums; run log |
| `driver/pl2303-3.3.11.152/` | The working driver (signed), exported by the release script with `pnputil /export-driver` from the build PC (gitignored, not in the repo) |
