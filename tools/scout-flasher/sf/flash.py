"""Flashing, with the two flashers that are proven on these radios.

V3 (PY32F071, bootloader 7.00.07): the vendored k5v3-flasher, imported
unchanged and driven exactly like its cli.py does; its printed progress is
captured for the progress bar and the log.

V1 (DP32G030, bootloader 2.00.06): the bundled K5TOOL.exe v1.8.0.0 with
`-wrflash <packed image>`, as used for every V1 flash so far. It runs in a
temp folder (it writes and rotates K5TOOL.log in its working directory);
progress comes from that log, success from exit code 0 + "Done".
"""

import contextlib
import io
import re
import shutil
import subprocess
import tempfile
import threading
import time
from pathlib import Path

from sf import paths

CREATE_NO_WINDOW = 0x08000000
V3_CONTACT_TIMEOUT_S = 30    # no "Device detected" by then: give up
V3_TOTAL_TIMEOUT_S = 120     # a whole flash takes ~10 s
V1_TIMEOUT_S = 240

_ACK_RE = re.compile(r"Packet2FlashWriteAck \{[^}]*?ChunkNumber=0x([0-9a-fA-F]+)")
_COUNT_RE = re.compile(r"ChunkCount=0x([0-9a-fA-F]+)")
_V3_PAGE_RE = re.compile(r"Programming page (\d+) / (\d+)")


def parse_k5tool_progress(log_text: str) -> tuple[int, int] | None:
    """(chunks acknowledged, total chunks) from K5TOOL.log, or None."""
    acks = _ACK_RE.findall(log_text)
    counts = _COUNT_RE.findall(log_text)
    if not acks or not counts:
        return None
    return max(int(a, 16) for a in acks) + 1, int(counts[-1], 16)


def flash_v1(port: str, packed_image: Path, progress, log, cancel: threading.Event,
             k5tool_cmd: list[str] | None = None) -> bool:
    cmd = list(k5tool_cmd or [str(paths.k5tool_exe())])
    with tempfile.TemporaryDirectory(prefix="scoutflash-", ignore_cleanup_errors=True) as tmp:
        work = Path(tmp)
        shutil.copyfile(packed_image, work / "fw.packed.bin")
        args = cmd + ["-port", port, "-wrflash", "fw.packed.bin"]
        log("Run: {}".format(" ".join(args)))
        proc = subprocess.Popen(args, cwd=work, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, creationflags=CREATE_NO_WINDOW,
                                text=True, encoding="utf-8", errors="replace")
        output: list[str] = []
        reader = threading.Thread(target=lambda: output.extend(proc.stdout), daemon=True)
        reader.start()

        k5log = work / "K5TOOL.log"
        last = None
        deadline = time.monotonic() + V1_TIMEOUT_S
        while proc.poll() is None:
            if cancel.is_set() or time.monotonic() > deadline:
                log("Stopping K5TOOL ({})".format("cancelled" if cancel.is_set() else "timeout"))
                proc.kill()
                break
            last = _report_k5tool(k5log, last, progress)
            time.sleep(0.2)
        proc.wait()
        reader.join(timeout=5)
        _report_k5tool(k5log, last, progress)

        out = "".join(output)
        log("K5TOOL exit code {}\n{}".format(proc.returncode, out.rstrip()))
        if k5log.exists():
            log("---- K5TOOL.log ----\n" + k5log.read_text(encoding="utf-8", errors="replace").rstrip())
        return proc.returncode == 0 and "Done" in out


def _report_k5tool(k5log: Path, last, progress):
    try:
        p = parse_k5tool_progress(k5log.read_text(encoding="utf-8", errors="replace"))
    except OSError:
        return last
    if p and p != last:
        progress(*p)
    return p or last


class _Lines(io.TextIOBase):
    """A stdout replacement that hands complete lines to a callback."""

    def __init__(self, on_line):
        self._on_line = on_line
        self._buf = ""

    def writable(self):
        return True

    def write(self, s):
        self._buf += s
        while "\n" in self._buf:
            line, self._buf = self._buf.split("\n", 1)
            self._on_line(line)
        return len(s)

    def flush_rest(self):
        if self._buf:
            self._on_line(self._buf)
            self._buf = ""


def flash_v3(port: str, image: bytes, progress, log, cancel: threading.Event,
             bl_ver: str = "7.00") -> bool:
    """bl_ver: the bootloader's version family (first 4 chars, e.g. "7.00"),
    which the ProtocolV2 handshake must echo (see the k5v3-flasher README)."""
    paths.enable_k5v3_imports()
    import serial
    import _prog as pp   # vendored k5v3-flasher, unchanged

    state = {"contact": False}

    def on_line(line: str):
        log(line)
        if "Device detected" in line:
            state["contact"] = True
        m = _V3_PAGE_RE.search(line)
        if m:
            progress(int(m.group(1)) - 1, int(m.group(2)))

    out = _Lines(on_line)
    # Same serial settings as the vendored cli.py.
    ser = serial.Serial(port, baudrate=38400, timeout=0.0001, write_timeout=None)
    try:
        with contextlib.redirect_stdout(out):
            prog = pp.Programmer(ser, image, bl_ver)
            started = time.monotonic()
            while not cancel.is_set() and prog.loop():
                elapsed = time.monotonic() - started
                if not state["contact"] and elapsed > V3_CONTACT_TIMEOUT_S:
                    print("No contact with the radio after {} s".format(V3_CONTACT_TIMEOUT_S))
                    break
                if elapsed > V3_TOTAL_TIMEOUT_S:
                    print("Flash not finished after {} s".format(V3_TOTAL_TIMEOUT_S))
                    break
                time.sleep(0)
        out.flush_rest()
    finally:
        ser.close()
    if prog.ok:
        pages = (len(image) + 255) // 256
        progress(pages, pages)
    return bool(prog.ok)
