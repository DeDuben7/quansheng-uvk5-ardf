"""ScoutFlasher entry point.

    ScoutFlasher.exe                       the wizard (real cable and radio)
    ScoutFlasher.exe --simulate <scenario> the wizard on fake hardware (dev)
    ScoutFlasher.exe --fix-driver <log>    elevated helper: force the PL2303
                                           driver (started by the wizard)

The firmware is not bundled: the wizard downloads the latest GitHub release
into a temporary folder when it starts (removed again on exit).

Scenarios: ok, no-cable, driver-fix, driver-stuck, no-radio, unknown-bl,
flash-fail, offline, needs-new-exe, newer-exe, bad-download.
"""

import platform
import shutil
import sys
import tempfile
import tkinter as tk
from pathlib import Path
from tkinter import messagebox

from sf import buildinfo, paths
from sf.backend import SCENARIOS, RealBackend, SimBackend
from sf.log import RunLog


def fix_driver_main(log_path: str) -> int:
    from sf import driver
    log = RunLog(Path(log_path))
    try:
        return driver.fix_driver_here(paths.driver_inf(), log.write)
    except Exception as e:  # noqa: BLE001 - report everything to the parent's log
        log.write("fix-driver crashed: {!r}".format(e))
        return 3
    finally:
        log.close()


_instance_mutex = None


def already_running() -> bool:
    """A second copy would fight the first over the COM port. The one-file
    EXE shows nothing for a few seconds while it unpacks, so double
    clicking twice is easy."""
    global _instance_mutex
    import ctypes
    from ctypes import wintypes
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.CreateMutexW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.LPCWSTR]
    k32.CreateMutexW.restype = wintypes.HANDLE
    _instance_mutex = k32.CreateMutexW(None, False, "Local\\ScoutFlasher-SingleInstance")
    return ctypes.get_last_error() == 183   # ERROR_ALREADY_EXISTS


def main(argv: list[str]) -> int:
    if len(argv) == 2 and argv[0] == "--fix-driver":
        return fix_driver_main(argv[1])

    if already_running():
        root = tk.Tk()
        root.withdraw()
        messagebox.showinfo("ScoutFlasher", "Het programma is al geopend. Kijk in de taakbalk.")
        return 0

    scenario = None
    if len(argv) == 2 and argv[0] == "--simulate":
        scenario = argv[1]
        if scenario not in SCENARIOS:
            print("scenario: " + ", ".join(SCENARIOS))
            return 2

    try:
        import ctypes
        # sharp text on scaled (high-DPI) screens; the UI scales its pixel sizes
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
        # own taskbar identity, so the fox shows (not python.exe's icon) when run from source
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID("Scout.ScoutFlasher")
    except (AttributeError, OSError):
        pass
    root = tk.Tk()

    log_dir = Path(tempfile.gettempdir()) if scenario else paths.desktop_dir()
    log = RunLog(log_dir / RunLog.default_name())
    work_dir = Path(tempfile.mkdtemp(prefix="scoutflasher-"))   # the downloaded release lives here
    try:
        built = " (built {})".format(buildinfo.BUILD_DATE) if buildinfo.BUILD_DATE else ""
        mode = "simulate " + scenario if scenario else "real"
        log.section("{}{} ({})".format(buildinfo.label(), built, mode))
        log.write("Windows: {} {}".format(platform.platform(), platform.version()))
        log.write("Python {} frozen={}".format(platform.python_version(), paths.is_frozen()))
        log.write("Work folder: {}".format(work_dir))

        if scenario is None:
            backend = RealBackend(log, work_dir)
        else:
            backend = SimBackend(log, scenario, work_dir)

        from sf.ui import App
        App(root, backend, log, help_after_s=4 if scenario else 15)
        root.mainloop()
    finally:
        log.write("Closed")
        log.close()
        shutil.rmtree(work_dir, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
