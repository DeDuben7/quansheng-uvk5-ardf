# Changelog

Newest first. `tools/release.ps1 -Publish` uses the `## X.Y` section of a
release as its notes on GitHub.

## 1.1

- ScoutFlasher is online only. At every start it downloads the newest
  firmware from the latest GitHub release and checks its size and SHA-256
  before it flashes anything. It has no firmware built in, so an old copy can
  never downgrade a radio. Without an internet connection it does not flash.
- ScoutFlasher tells you when a newer version of the program is available,
  and refuses to flash when it is too old for the latest firmware.
- ScoutFlasher has its own icon, a fox.
- Every firmware image and ScoutFlasher.exe carry the git commit they were
  built from: on the radio's Info screen (version and commit, such as
  `v1.1 1a2b3c4`), on the manual's cover, in ScoutFlasher's footer and in the
  EXE's file properties.
- A drawing of the radio replaces the photo in the manual and in
  ScoutFlasher.
- The project is public under the Apache-2.0 licence. NOTICE credits the
  upstream firmware projects and the programs bundled into ScoutFlasher.
  LICENSE and NOTICE also travel inside `scout-firmware.zip` and
  `ScoutFlasher.exe`.
- ScoutFlasher's downloads also work on PCs where Windows has not yet fetched
  GitHub's root certificate, or where an antivirus program inspects HTTPS
  traffic. Certificates and host names are still checked.
- The manual says to throw away old copies of ScoutFlasher and what to do
  about Edge's download warning. The "program too old" and "new program
  available" screens now say to close the program before starting the new one.

## 1.0

- Settings menu: squelch, modulation (FM/AM), band (2 m / 70 cm), scanner
  and an Info screen.
- Adjustable squelch (0-9), based on the radio's factory calibration, with a
  live squelch readout on the Info screen.
- Every power-on starts on 144.450 MHz.
- The firmware has a version number, shown on the welcome screen and in Info.
- The first ScoutFlasher: a one-click Windows updater for V1 and V3 radios
  that installs the cable driver and recognises the radio by itself.
- A Dutch manual (PDF).
