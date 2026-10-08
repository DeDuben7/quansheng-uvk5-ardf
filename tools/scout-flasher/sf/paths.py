"""Where things are, both from the source tree and inside the PyInstaller EXE."""

import ctypes
import sys
import uuid
from ctypes import wintypes
from pathlib import Path

_TOOL_DIR = Path(__file__).resolve().parent.parent   # tools/scout-flasher


def is_frozen() -> bool:
    return getattr(sys, "frozen", False)


def resource_dir() -> Path:
    """Folder holding bin/, driver/, img/ and icon/ (the firmware is downloaded, not bundled)."""
    if is_frozen():
        return Path(sys._MEIPASS)   # noqa: SLF001 - PyInstaller's unpack dir
    return _TOOL_DIR


def k5tool_exe() -> Path:
    return resource_dir() / "bin" / "K5TOOL.exe"


def driver_inf() -> Path:
    return resource_dir() / "driver" / "pl2303-3.3.11.152" / "ser2pl.inf"


def image(name: str) -> Path:
    """Pictures for the UI; shared with the manual (docs/manual/img)."""
    if is_frozen():
        return resource_dir() / "img" / name
    return _TOOL_DIR.parent.parent / "docs" / "manual" / "img" / name


def icon_ico() -> Path:
    """The fox, multi-size (window title bar / taskbar on Windows)."""
    return resource_dir() / "icon" / "scoutflasher.ico"


def icon_png() -> Path:
    """The fox, 256 px (Tk PhotoImage fallback)."""
    return resource_dir() / "icon" / "scoutflasher.png"


def k5v3_dir() -> Path:
    """The vendored V3 flasher (source tree only; the EXE has it built in)."""
    return _TOOL_DIR.parent.parent / "platform" / "uv-k5-v3-py32f071" / "tools" / "k5v3-flasher"


def enable_k5v3_imports() -> None:
    """Make `import _prog` / `import msg` work when running from source."""
    if not is_frozen():
        d = str(k5v3_dir())
        if d not in sys.path:
            sys.path.insert(0, d)


class _GUID(ctypes.Structure):
    _fields_ = [("Data1", wintypes.DWORD), ("Data2", wintypes.WORD),
                ("Data3", wintypes.WORD), ("Data4", ctypes.c_ubyte * 8)]


_FOLDERID_DESKTOP = "{B4BFCC3A-DB2C-424C-B029-7FE99A87C641}"


def desktop_dir() -> Path:
    """The user's real desktop (also when OneDrive redirects it)."""
    try:
        u = uuid.UUID(_FOLDERID_DESKTOP)
        guid = _GUID.from_buffer_copy(u.bytes_le)
        out = ctypes.c_wchar_p()
        hr = ctypes.windll.shell32.SHGetKnownFolderPath(ctypes.byref(guid), 0, None, ctypes.byref(out))
        if hr == 0 and out.value:
            path = Path(out.value)
            ctypes.windll.ole32.CoTaskMemFree(out)
            return path
    except (AttributeError, OSError, ValueError):
        pass
    return Path.home() / "Desktop"
