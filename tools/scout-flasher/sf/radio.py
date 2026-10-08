"""Finding the radio: the COM port of the cable, and the bootloader beacon.

A UV-K5 in bootloader mode (PTT held at power-on, flashlight LED on)
repeats a device-info beacon (message 0x0518) at 38400 baud. V1 and V3
share the packet framing (AB CD ... DC BA, obfuscated); the vendored
k5v3-flasher's msg.fetch() decodes both. The bootloader version string at
offset 20 tells them apart: 2.x = V1 (DP32G030), 7.x = V3 (PY32F071).
"""

import threading
import time
from dataclasses import dataclass

import serial
from serial.tools import list_ports

from sf import paths

paths.enable_k5v3_imports()
import msg as mm  # noqa: E402  (vendored k5v3-flasher)

BAUD = 38400

# USB-serial chips used in K-plug programming cables, ours (Prolific) first.
CABLE_VIDS = (0x067B, 0x1A86, 0x10C4, 0x0403)


@dataclass(frozen=True)
class Beacon:
    version: str
    kind: str | None    # "V1", "V3", or None for a bootloader we don't know


def classify(version: str) -> str | None:
    if version.startswith("7."):
        return "V3"
    if version.startswith("2."):
        return "V1"
    return None


def bl_version(m) -> str:
    """Bootloader version from a device-info message (as _prog does)."""
    buf = m.buf
    end = buf.find(b"\x00", 20, 36)
    if end == -1:
        end = 36
    return bytes(buf[20:end]).decode("ascii", "replace")


def parse_beacons(buf: bytearray) -> list[Beacon]:
    """All complete beacons in buf (consumed); partial packets stay."""
    found = []
    while True:
        before = len(buf)
        m = mm.fetch(buf)
        if m is None:
            # fetch() returns None both for "need more bytes" (nothing
            # consumed) and after discarding junk (try again).
            if len(buf) == before:
                return found
            continue
        if m.get_msg_type() == mm.MSG_NOTIFY_DEV_INFO:
            v = bl_version(m)
            found.append(Beacon(v, classify(v)))


def find_ports(comports=list_ports.comports) -> list[str]:
    """COM ports of programming cables, Prolific first."""
    ports = [p for p in comports() if p.vid in CABLE_VIDS]
    ports.sort(key=lambda p: CABLE_VIDS.index(p.vid))
    return [p.device for p in ports]


def wait_for_beacon(port: str, timeout_s: float, cancel: threading.Event,
                    open_serial=serial.Serial, min_beacons: int = 2) -> Beacon | None:
    """Listen until `min_beacons` beacons agree, or timeout/cancel (None).

    Raises serial.SerialException if the port can't be opened.
    """
    deadline = time.monotonic() + timeout_s
    buf = bytearray()
    last, count = None, 0
    with open_serial(port, BAUD, timeout=0.05) as ser:
        while not cancel.is_set() and time.monotonic() < deadline:
            data = ser.read(256)
            if not data:
                continue
            buf.extend(data)
            for b in parse_beacons(buf):
                count = count + 1 if b == last else 1
                last = b
                if count >= min_beacons:
                    return b
    return None
