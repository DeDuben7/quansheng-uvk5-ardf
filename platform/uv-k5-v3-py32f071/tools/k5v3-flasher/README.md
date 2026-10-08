# k5v3-flasher

Local CLI flasher for the UV-K5 **V3 / PY32F071** ProtocolV2 bootloader.

Quansheng's V3 bootloader (`7.00.07`) speaks "ProtocolV2", which
`K5TOOL.exe -wrflashraw` (v1.8.0.0) cannot complete — its block write is never
acknowledged and times out. This tool implements the ProtocolV2 flash the same
way the UVTools2 web flasher (`js/flash.js`, the reference) does: 38400 baud,
sync to the device-info beacon, ≤4-char BL-version handshake, 200 ms settle,
then fixed 268-byte PROG_FW messages (256-byte pages), each waited on for its
PROG_FW_RESP ACK before the next is sent.

## Source & license

Vendored from **muzkr**'s `serialtool`, bundled in
`armel/uv-k1-k5v3-firmware-custom` under `tools/serialtool/`.
Copyright (c) 2025 muzkr — **MIT License** (headers preserved in each file).
Only the flash path is vendored (`cli.py`, `_prog.py`, `msg.py`, `_fwcodec.py`);
the dump/restore subcommands are unused here.
The headers refer to "the root of this repository" for the licence; the MIT
licence text is in [`LICENSE`](LICENSE), next to this README.

## scout modifications

- **`msg.fetch()` split-packet fix (root cause of the old stalls).** The port
  is polled with a 0.1 ms timeout, so a packet often arrives across two reads.
  Upstream checked the `DC BA` end marker before the whole packet was in the
  buffer. A partial packet failed that check and its header was deleted, so
  the packet was silently lost. When that packet was a page ACK, the flash
  hung at a random page (page 4 of 53 in one run). `fetch()` now waits for the
  full packet, like UVTools2's `fetchMessage`.
- **Bounded ACK wait** (`_prog.py`), copied from UVTools2: resend a page after
  3 s without an ACK, and fail after 3 consecutive resends. Upstream waits
  forever. With the fix above, resends should not happen. If you see any,
  suspect the cable or port.
- **No page padding.** An earlier workaround padded the image to 256 bytes
  "because the last page wasn't ACKed". That was the same lost-ACK bug. Every
  PROG_FW message is a fixed 268 bytes with a zero-filled tail, so a short
  last page is fine (UVTools2 doesn't pad either).
- `--verbose` hex trace, a summary line (`N pages in X s, K resends`), and a
  non-zero exit code on failure (so `make flash` fails visibly).
- The BL-version warning compares `--bl-ver` as a prefix of the reported
  version (`7.00` vs `7.00.07` no longer warns).

## Usage

Requires Python 3 + `pyserial` (`pip install pyserial`). Radio in bootloader
mode (hold PTT while powering on → white flashlight LED steady; it blinks
while pages are written).

From the project folder (`uv-k5-v3-py32f071/`):

```bash
make flash                       # rebuilds, then flashes to COM9, BLVER=7.00
make flash PORT=COM7             # different port
make flash BLVER=1.05            # different bootloader family
```

or directly:

```bash
python tools/k5v3-flasher/cli.py flash --port COM9 --bl-ver "7.00" firmware_uvk5_v3.bin
python tools/k5v3-flasher/cli.py flash --port COM9 --bl-ver "7.00" --verbose firmware_uvk5_v3.bin
```

Expected: `Firmware program done: 55 pages in ~8 s, 0 resends` for the
~14 KB scout image. `--verbose` prints one line per packet:
`[t] TX|RX type=0x.... len=... | <raw on-the-wire hex>` (the payload is
XOR-obfuscated on the wire).

`--bl-ver` is the handshake bootloader version and **must match the radio's
bootloader family (first ≤4 chars) to unlock programming.** `?` does *not*
unlock it — the handshake appears to complete but the bootloader silently
ignores page writes. This radio reports bootloader `7.00.07`, so `7.00` works.

If a flash fails midway, the radio stays in the bootloader. Re-flash with this
tool or UVTools2.

## Tests

Offline, no radio needed (from this folder):

```bash
python -m unittest test_msg
```

Covers packet framing (including packets split at every byte offset) and a
full flash against a fake bootloader that returns bytes in random small
chunks and keeps beaconing during programming.
