# PyInstaller spec for ScoutFlasher.exe (one file, windowed).
#
#   python -m PyInstaller --noconfirm --clean tools/scout-flasher/scout-flasher.spec
#
# The EXE carries no firmware: it downloads the latest release when it starts.
# tools/release.ps1 builds it and copies it to release/<version>/ScoutFlasher.exe.
# It first exports driver/pl2303-3.3.11.152 from the build PC (gitignored, not
# in the repo), so a build from a fresh clone needs that step (or the folder).
# sf/update.py imports certifi on purpose: PyInstaller's certifi hook then bundles
# cacert.pem, which the HTTPS downloads use next to the Windows certificate store.
from pathlib import Path

here = Path(SPECPATH)
repo = here.parent.parent
k5v3 = repo / "platform" / "uv-k5-v3-py32f071" / "tools" / "k5v3-flasher"

a = Analysis(
    [str(here / "scout_flasher.py")],
    pathex=[str(here), str(k5v3)],
    # The vendored V3 flasher is imported by name (sf.flash / sf.radio).
    hiddenimports=["_prog", "msg", "_fwcodec", "serial.tools.list_ports"],
    datas=[
        (str(here / "bin"), "bin"),
        (str(here / "driver"), "driver"),
        (str(repo / "docs" / "manual" / "img" / "porto-front.png"), "img"),
        (str(here / "icon" / "scoutflasher.ico"), "icon"),
        (str(here / "icon" / "scoutflasher.png"), "icon"),
        # Apache-2.0 section 4: the licence and the credits go with the binary.
        (str(repo / "LICENSE"), "."),
        (str(repo / "NOTICE"), "."),
    ],
    excludes=["pytest"],
    noarchive=False,
)
pyz = PYZ(a.pure)
# Windows file properties (version, company); tools/release.ps1 generates it.
version_file = here / "build" / "version_info.txt"
exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name="ScoutFlasher",
    console=False,
    upx=False,
    debug=False,
    strip=False,
    icon=str(here / "icon" / "scoutflasher.ico"),
    version=str(version_file) if version_file.exists() else None,
)
