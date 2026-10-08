"""ScoutFlasher: one-click firmware updater for the scout portos (UV-K5 V1/V3).

Modules:
    paths     where the bundled resources (icon, drivers, K5TOOL, images) live; the
              firmware is not bundled: it is downloaded at every run
    manifest  firmware manifest: version + SHA-256 of the downloaded files
    update    downloads the latest GitHub release and verifies it (update.fetch_latest)
    log       the run log written to the user's desktop
    driver    finds the USB cable, checks/forces the PL2303 driver
    radio     finds the COM port, reads the bootloader beacon (V1 or V3)
    flash     flashes V3 (vendored k5v3-flasher) or V1 (bundled K5TOOL.exe)
    backend   the steps the UI drives, real or simulated
    ui        the Dutch wizard (Tkinter)
    buildinfo which build this is (commit + date, written by tools/release.ps1)
"""

# The version of the flasher program itself (shown in the footer and the
# EXE properties). Bump only when the flasher program itself changes; the
# firmware has its own version in the manifest.
FLASHER_VERSION = "1.1"
