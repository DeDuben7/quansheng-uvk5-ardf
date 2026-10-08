"""The programming cable's USB driver.

The scout cable is a Prolific PL2303 with an old HXA-class chip
(hardware ID USB\\VID_067B&PID_2303&REV_0300). It works with Prolific's
2010 driver 3.3.11.152. The 2024 driver (3.9.6.0) that Windows Update
installs rejects these chips ("PHASED OUT SINCE 2012", Code 10). So for
that cable we make sure 3.3.11.152 is active, and force it if not: the
newer driver would otherwise always win on date. We check on every run,
so a driver swapped back by Windows Update gets fixed again.

Other cables (CH340, CP210x, FTDI, newer PL2303) are left alone.

Two different problems, kept apart so the user is never sent round in a
loop: the WRONG driver (needs_fix: we can force the right one) and the
RIGHT driver that still won't start (driver_problem: replug / restart, or
Windows' Memory Integrity blocking the 2010 driver - not ours to change).
"""

import ctypes
import json
import re
import subprocess
import sys
from ctypes import wintypes
from dataclasses import dataclass
from pathlib import Path

GOOD_DRIVER = "3.3.11.152"
PL2303_HWID = "USB\\VID_067B&PID_2303"

CREATE_NO_WINDOW = 0x08000000

# Exit codes of the elevated --fix-driver helper, as seen by the wizard.
FIX_OK = 0
FIX_PNPUTIL_FAILED = 1
FIX_UPDATE_FAILED = 2
FIX_CRASHED = 3
FIX_REBOOT_NEEDED = 4
FIX_CANCELLED = -1223          # user said No to the UAC prompt
FIX_NOT_STARTED = -1           # ShellExecuteEx failed for another reason

# Console tools (pnputil, PowerShell) write in the OEM code page.
_TEXT = dict(encoding="oem", errors="replace", stdin=subprocess.DEVNULL,
             creationflags=CREATE_NO_WINDOW)

_QUERY = r"""
$ErrorActionPreference = 'SilentlyContinue'
$vids = 'VID_067B','VID_1A86','VID_10C4','VID_0403'
$devs = Get-PnpDevice -PresentOnly | Where-Object {
    $i = $_.InstanceId; $i -like 'USB\*' -and ($vids | Where-Object { $i -match $_ })
}
@($devs | ForEach-Object {
    $p = Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName `
        DEVPKEY_Device_DriverVersion, DEVPKEY_Device_ProblemCode, DEVPKEY_Device_HardwareIds
    [pscustomobject]@{
        InstanceId    = $_.InstanceId
        FriendlyName  = $_.FriendlyName
        Status        = [string]$_.Status
        Problem       = ($p | Where-Object KeyName -eq 'DEVPKEY_Device_ProblemCode').Data
        DriverVersion = ($p | Where-Object KeyName -eq 'DEVPKEY_Device_DriverVersion').Data
        HardwareIds   = @(($p | Where-Object KeyName -eq 'DEVPKEY_Device_HardwareIds').Data)
    }
}) | ConvertTo-Json -Depth 3
"""


@dataclass
class Cable:
    instance_id: str
    vid: str
    pid: str
    rev: str | None
    status: str
    problem: int
    driver_version: str | None
    com_port: str | None
    friendly_name: str = ""

    @property
    def is_scout_pl2303(self) -> bool:
        return self.vid == "067B" and self.pid == "2303" and self.rev == "0300"

    def describe(self) -> str:
        return "{} VID_{}&PID_{} REV_{} status={} problem={} driver={} port={}".format(
            self.friendly_name, self.vid, self.pid, self.rev, self.status,
            self.problem, self.driver_version, self.com_port)


def _parse(item: dict) -> Cable:
    ids = item.get("HardwareIds") or []
    if isinstance(ids, str):
        ids = [ids]
    text = " ".join(ids) + " " + (item.get("InstanceId") or "")
    vid = re.search(r"VID_([0-9A-F]{4})", text, re.I)
    pid = re.search(r"PID_([0-9A-F]{4})", text, re.I)
    rev = re.search(r"REV_([0-9A-F]{4})", text, re.I)
    name = item.get("FriendlyName") or ""
    com = re.search(r"\((COM\d+)\)", name)
    return Cable(
        instance_id=item.get("InstanceId") or "",
        vid=vid.group(1).upper() if vid else "",
        pid=pid.group(1).upper() if pid else "",
        rev=rev.group(1).upper() if rev else None,
        status=item.get("Status") or "",
        problem=int(item.get("Problem") or 0),
        driver_version=item.get("DriverVersion"),
        com_port=com.group(1) if com else None,
        friendly_name=name,
    )


def query_cables(run=subprocess.run) -> list[Cable]:
    """Connected USB-serial cables, via PowerShell's PnP cmdlets."""
    res = run(["powershell", "-NoProfile", "-NonInteractive", "-Command", _QUERY],
              capture_output=True, timeout=60, **_TEXT)
    out = (res.stdout or "").strip()
    if not out:
        return []
    data = json.loads(out)
    if isinstance(data, dict):
        data = [data]
    return [_parse(d) for d in data]


def needs_fix(c: Cable) -> bool:
    """The scout cable has the wrong driver: forcing ours can fix it."""
    return c.is_scout_pl2303 and c.driver_version != GOOD_DRIVER


def driver_problem(c: Cable) -> bool:
    """The scout cable has the right driver, but Windows reports a problem
    (needs a replug/restart, or the driver is blocked)."""
    return c.is_scout_pl2303 and c.driver_version == GOOD_DRIVER and c.problem != 0


_HVCI_QUERY = (
    "(Get-CimInstance -Namespace root\\Microsoft\\Windows\\DeviceGuard -ClassName Win32_DeviceGuard "
    "-ErrorAction SilentlyContinue).SecurityServicesRunning -contains 2")


def memory_integrity_on(run=subprocess.run) -> bool | None:
    """Windows' Memory Integrity (HVCI) running? None if unknown. It can
    block old kernel drivers such as the 2010 PL2303 driver."""
    try:
        res = run(["powershell", "-NoProfile", "-NonInteractive", "-Command", _HVCI_QUERY],
                  capture_output=True, timeout=60, **_TEXT)
    except (OSError, subprocess.SubprocessError):
        return None
    out = (res.stdout or "").strip()
    return {"True": True, "False": False}.get(out)


# --- Elevation ---------------------------------------------------------------

class _SHELLEXECUTEINFOW(ctypes.Structure):
    _fields_ = [
        ("cbSize", wintypes.DWORD), ("fMask", ctypes.c_ulong), ("hwnd", wintypes.HWND),
        ("lpVerb", wintypes.LPCWSTR), ("lpFile", wintypes.LPCWSTR),
        ("lpParameters", wintypes.LPCWSTR), ("lpDirectory", wintypes.LPCWSTR),
        ("nShow", ctypes.c_int), ("hInstApp", wintypes.HINSTANCE), ("lpIDList", ctypes.c_void_p),
        ("lpClass", wintypes.LPCWSTR), ("hkeyClass", wintypes.HKEY), ("dwHotKey", wintypes.DWORD),
        ("hIconOrMonitor", wintypes.HANDLE), ("hProcess", wintypes.HANDLE),
    ]


def _self_command(args: str) -> tuple[str, str]:
    """(file, parameters) to start this program again with `args`."""
    if getattr(sys, "frozen", False):
        return sys.executable, args
    script = Path(__file__).resolve().parent.parent / "scout_flasher.py"
    return sys.executable, '"{}" {}'.format(script, args)


_shell32 = ctypes.WinDLL("shell32", use_last_error=True)
_shell32.ShellExecuteExW.argtypes = [ctypes.POINTER(_SHELLEXECUTEINFOW)]
_shell32.ShellExecuteExW.restype = wintypes.BOOL
_kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
_kernel32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
_kernel32.WaitForSingleObject.restype = wintypes.DWORD
_kernel32.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
_kernel32.GetExitCodeProcess.restype = wintypes.BOOL
_kernel32.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
_kernel32.CloseHandle.argtypes = [wintypes.HANDLE]

HELPER_TIMEOUT_MS = 10 * 60 * 1000


def fix_driver_elevated(helper_log: Path) -> int:
    """Run `--fix-driver` as administrator (one UAC prompt); its exit code
    (FIX_*). FIX_CANCELLED if the user declined the prompt."""
    SEE_MASK_NOCLOSEPROCESS = 0x00000040
    exe, params = _self_command('--fix-driver "{}"'.format(helper_log))
    info = _SHELLEXECUTEINFOW()
    info.cbSize = ctypes.sizeof(info)
    info.fMask = SEE_MASK_NOCLOSEPROCESS
    info.lpVerb = "runas"
    info.lpFile = exe
    info.lpParameters = params
    info.nShow = 0   # SW_HIDE
    if not _shell32.ShellExecuteExW(ctypes.byref(info)):
        err = ctypes.get_last_error()
        return FIX_CANCELLED if err == 1223 else FIX_NOT_STARTED
    try:
        if _kernel32.WaitForSingleObject(info.hProcess, HELPER_TIMEOUT_MS) != 0:   # not WAIT_OBJECT_0
            _kernel32.TerminateProcess(info.hProcess, 1)
            return FIX_CRASHED
        code = wintypes.DWORD()
        if not _kernel32.GetExitCodeProcess(info.hProcess, ctypes.byref(code)):
            return FIX_CRASHED
        return ctypes.c_int32(code.value).value
    finally:
        _kernel32.CloseHandle(info.hProcess)


def fix_driver_here(inf: Path, log) -> int:
    """Inside the elevated helper: stage the 2010 driver and force it.
    Returns FIX_OK, FIX_REBOOT_NEEDED, FIX_PNPUTIL_FAILED or FIX_UPDATE_FAILED.
    """
    log("Driver package: {}".format(inf))
    res = subprocess.run(["pnputil", "/add-driver", str(inf)], capture_output=True, **_TEXT)
    log("pnputil /add-driver -> exit {}\n{}".format(res.returncode, (res.stdout or "") + (res.stderr or "")))
    # 259: already in the driver store; 3010: staged, reboot requested.
    if res.returncode not in (0, 259, 3010):
        return FIX_PNPUTIL_FAILED

    # Force it onto every connected PL2303 (the INF only lists
    # VID_067B&PID_2303). Only called when the scout cable (REV_0300) needs it.
    INSTALLFLAG_FORCE = 0x00000001
    newdev = ctypes.WinDLL("newdev.dll", use_last_error=True)
    update = newdev.UpdateDriverForPlugAndPlayDevicesW
    update.argtypes = [wintypes.HWND, wintypes.LPCWSTR, wintypes.LPCWSTR,
                       wintypes.DWORD, ctypes.POINTER(wintypes.BOOL)]
    update.restype = wintypes.BOOL
    reboot = wintypes.BOOL(False)
    ok = update(None, PL2303_HWID, str(inf), INSTALLFLAG_FORCE, ctypes.byref(reboot))
    if not ok:
        log("UpdateDriverForPlugAndPlayDevices failed, error {}".format(ctypes.get_last_error()))
        return FIX_UPDATE_FAILED
    log("Driver {} installed{}".format(GOOD_DRIVER, " (reboot requested)" if reboot.value else ""))
    return FIX_REBOOT_NEEDED if reboot.value else FIX_OK
