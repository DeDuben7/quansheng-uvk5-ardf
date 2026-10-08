"""The steps the wizard drives, behind one interface:

    prepare()                -> update.Package  (download + verify the latest
                                                 release; raises UpdateError)
    check_cable()            -> CableResult
    fix_driver()             -> int           (driver.FIX_*; one UAC prompt)
    wait_radio(port, cancel) -> Beacon | None (None = cancelled)
    flash(port, beacon, progress, cancel) -> bool  (re-verifies the package first;
                                                 raises PackageDamaged if it changed)

RealBackend talks to the cable and radio; SimBackend fakes a scenario so
every screen and error path can be clicked through without hardware
(`scout_flasher.py --simulate <scenario>`). All calls block; the UI runs
them on a worker thread.
"""

import shutil
import tempfile
import threading
import time
from dataclasses import dataclass
from pathlib import Path

from sf import FLASHER_VERSION, driver, flash, radio, update
from sf.manifest import verify, write_manifest
from sf.radio import Beacon

SCENARIOS = ("ok", "no-cable", "driver-fix", "driver-stuck", "no-radio", "unknown-bl", "flash-fail",
             "offline", "needs-new-exe", "newer-exe", "bad-download")

SIM_FETCH_DELAY = 1.0      # seconds the simulated download "takes" (tests set it to 0)


@dataclass
class CableResult:
    # "ok"             port is set
    # "needs_fix"      the scout cable has the wrong driver (we can fix it)
    # "driver_problem" right driver, but Windows can't start it (problem code;
    #                  memory_integrity tells whether HVCI may be blocking it)
    # "not_found"      no cable
    # "problem"        another cable with a problem
    state: str
    port: str | None = None
    problem: int = 0
    memory_integrity: bool | None = None


class PackageDamaged(RuntimeError):
    """The downloaded firmware package no longer matches its manifest (checked
    again right before flashing). Nothing was sent to the radio."""


class RealBackend:

    def __init__(self, log, work_dir: Path):
        self.log = log
        self.work_dir = Path(work_dir)      # where the downloaded release is unpacked
        self.package: update.Package | None = None
        self._hvci = None
        self._hvci_checked = False

    def prepare(self, log=None) -> update.Package:
        """Download and verify the latest release (blocking). Raises update.UpdateError."""
        write = log or self.log.write
        pkg = update.fetch_latest(self.work_dir, FLASHER_VERSION, write)
        self.package = pkg
        i, m = pkg.info, pkg.manifest
        write("Package: release {} commit {} date {} (flasher {}, needs >= {})".format(
            i.release, i.commit, i.date, i.flasher_version, i.min_flasher))
        write("Firmware: V1 {}  V3 {}  manual {}".format(m.v1_file, m.v3_file, m.manual))
        for name, digest in m.sha256.items():
            write("  sha256 {} {}".format(digest, name))
        return pkg

    def _memory_integrity(self) -> bool | None:
        if not self._hvci_checked:
            self._hvci = driver.memory_integrity_on()
            self._hvci_checked = True
            self.log.write("Memory Integrity (HVCI): {}".format(
                {True: "on", False: "off", None: "unknown"}[self._hvci]))
        return self._hvci

    def check_cable(self) -> CableResult:
        try:
            cables = driver.query_cables()
        except Exception as e:  # noqa: BLE001 - PowerShell trouble must not kill the UI
            self.log.write("Cable query failed: {!r}".format(e))
            cables = []
        ports = radio.find_ports()
        for c in cables:
            self.log.write("Cable: " + c.describe())
        if any(driver.needs_fix(c) for c in cables):
            return CableResult("needs_fix")
        stuck = [c for c in cables if driver.driver_problem(c)]
        if stuck:
            return CableResult("driver_problem", problem=stuck[0].problem,
                               memory_integrity=self._memory_integrity())
        usable = [c for c in cables if c.status == "OK" and c.problem == 0 and c.com_port]
        if usable:
            return CableResult("ok", usable[0].com_port)
        if ports:
            # pyserial sees a cable PowerShell didn't report: use it.
            self.log.write("Using {} (from the COM port list)".format(ports[0]))
            return CableResult("ok", ports[0])
        if cables:
            return CableResult("problem", problem=cables[0].problem)
        return CableResult("not_found")

    def fix_driver(self) -> int:
        self._memory_integrity()
        helper_log = self.log.path.with_name(self.log.path.stem + "-driver.txt")
        self.log.write("Starting elevated driver fix")
        code = driver.fix_driver_elevated(helper_log)
        self.log.write("Driver fix exit code {}".format(code))
        if helper_log.exists():
            self.log.write(helper_log.read_text(encoding="utf-8", errors="replace"))
            try:
                helper_log.unlink()
            except OSError:
                pass
        return code

    def wait_radio(self, port: str, cancel: threading.Event) -> Beacon | None:
        self.log.write("Listening for the bootloader on {}".format(port))
        while not cancel.is_set():
            b = radio.wait_for_beacon(port, 1.0, cancel)
            if b:
                self.log.write("Bootloader {} -> {}".format(b.version, b.kind or "unknown"))
                return b
        return None

    def flash(self, port: str, beacon: Beacon, progress, cancel: threading.Event) -> bool:
        if self.package is None:
            raise RuntimeError("flash() before prepare(): no firmware package")
        m, fw_dir = self.package.manifest, self.package.dir
        # fetch_latest verified the package when it was downloaded; check again right
        # before an image is read, in case the files changed on disk since then.
        # The UI shows an exception from here as its "something went wrong" screen.
        bad = verify(fw_dir, m)
        if bad:
            self.log.write("Package {} damaged since the download, not flashing: {}".format(
                self.package.info.release, ", ".join(bad)))
            raise PackageDamaged("firmware package changed on disk: " + ", ".join(bad))
        if beacon.kind == "V3":
            image = (fw_dir / m.v3_file).read_bytes()
            self.log.write("Flashing V3 with {} ({} bytes)".format(m.v3_file, len(image)))
            # Handshake with the bootloader's own version family ("7.00").
            return flash.flash_v3(port, image, progress, self.log.write, cancel,
                                  bl_ver=beacon.version[:4])
        if beacon.kind == "V1":
            self.log.write("Flashing V1 with {}".format(m.v1_file))
            return flash.flash_v1(port, fw_dir / m.v1_file, progress, self.log.write, cancel)
        raise ValueError("unknown radio " + beacon.version)


def _sim_pdf() -> bytes:
    """A tiny but valid one-page PDF, so [Handleiding] works in simulate mode."""
    stream = b"BT /F1 16 Tf 20 50 Td (Simulated manual) Tj ET"
    objs = [b"<</Type/Catalog/Pages 2 0 R>>",
            b"<</Type/Pages/Kids[3 0 R]/Count 1>>",
            b"<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 100]/Contents 4 0 R"
            b"/Resources<</Font<</F1 5 0 R>>>>>>",
            b"<</Length %d>>\nstream\n" % len(stream) + stream + b"\nendstream",
            b"<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>"]
    out, offsets = b"%PDF-1.4\n", []
    for n, body in enumerate(objs, 1):
        offsets.append(len(out))
        out += b"%d 0 obj\n" % n + body + b"\nendobj\n"
    xref = len(out)
    out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objs) + 1)
    out += b"".join(b"%010d 00000 n \n" % off for off in offsets)
    return out + b"trailer\n<</Root 1 0 R/Size %d>>\nstartxref\n%d\n%%%%EOF\n" % (len(objs) + 1, xref)


class SimBackend:
    """Fake hardware and a fake download for `--simulate <scenario>`."""

    def __init__(self, log, scenario: str = "ok", work_dir: Path | None = None):
        if scenario not in SCENARIOS:
            raise ValueError("scenario must be one of " + ", ".join(SCENARIOS))
        self.log = log
        self.scenario = scenario
        self.work_dir = Path(work_dir) if work_dir else None   # None: a fresh temp folder per prepare()
        self._fixed = False

    def prepare(self, log=None) -> update.Package:
        write = log or self.log.write
        write("[sim] fetching the latest release ({})".format(self.scenario))
        time.sleep(SIM_FETCH_DELAY)
        failures = {"offline": ("offline", "[sim] no network"),
                    "needs-new-exe": ("too_old", "[sim] this ScoutFlasher is older than min_flasher"),
                    "bad-download": ("bad_download", "[sim] the downloaded package is damaged")}
        if self.scenario in failures:
            err = update.UpdateError(*failures[self.scenario])
            write("Update failed ({}): {}".format(err.kind, err.message))
            raise err
        base = self.work_dir or Path(tempfile.mkdtemp(prefix="scoutflasher-sim-"))
        pkg_dir = base / "1.1"                  # like the real layout: <work dir>/<release>
        shutil.rmtree(pkg_dir, ignore_errors=True)
        pkg_dir.mkdir(parents=True)
        v1, v3, manual = "firmware_uvk5_v1.packed.bin", "firmware_uvk5_v3.bin", "Handleiding-Scout-porto.pdf"
        (pkg_dir / v1).write_bytes(b"sim V1 image")
        (pkg_dir / v3).write_bytes(b"sim V3 image")
        (pkg_dir / manual).write_bytes(_sim_pdf())
        manifest = write_manifest(pkg_dir, "1.1", v1, v3, manual)
        info = update.UpdateInfo(
            release="1.1", commit="sim0000", date="2026-10-08",
            flasher_version="1.2" if self.scenario == "newer-exe" else "1.1", min_flasher="1.1",
            zip_name="scout-firmware.zip", zip_size=0, zip_sha256="0" * 64)
        write("[sim] package {} in {}".format(info.release, pkg_dir))
        return update.Package(info, pkg_dir, manifest)

    def check_cable(self) -> CableResult:
        time.sleep(0.8)
        if self.scenario == "no-cable":
            return CableResult("not_found")
        if self.scenario in ("driver-fix", "driver-stuck") and not self._fixed:
            return CableResult("needs_fix")
        if self.scenario == "driver-stuck":
            return CableResult("driver_problem", problem=39, memory_integrity=True)
        return CableResult("ok", "COM9")

    def fix_driver(self) -> int:
        self.log.write("[sim] driver fix (no UAC prompt in simulate mode)")
        time.sleep(1.5)
        self._fixed = True
        return driver.FIX_OK

    def wait_radio(self, port: str, cancel: threading.Event) -> Beacon | None:
        delay = None if self.scenario == "no-radio" else 2.0
        start = time.monotonic()
        while not cancel.is_set():
            if delay is not None and time.monotonic() - start > delay:
                if self.scenario == "unknown-bl":
                    return Beacon("5.00.01", None)
                return Beacon("7.00.07", "V3")
            time.sleep(0.1)
        return None

    def flash(self, port: str, beacon: Beacon, progress, cancel: threading.Event) -> bool:
        total = 55
        for page in range(total + 1):
            if cancel.is_set():
                return False
            if self.scenario == "flash-fail" and page == 22:
                self.log.write("[sim] page 23 got no ACK after 3 retries")
                return False
            progress(page, total)
            time.sleep(0.05)
        return True
