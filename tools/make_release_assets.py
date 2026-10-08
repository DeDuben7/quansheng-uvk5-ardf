"""Write the fixed-name GitHub release assets (called by tools/release.ps1).

First call, after the firmware and the manual are built:

    python tools/make_release_assets.py --version 1.1 --commit 8b8060a --date 2026-10-08 \
        --flasher-version 1.1 --min-flasher 1.1 \
        --v1 platform/uv-k5-v1-dp32g030/firmware_uvk5_v1.packed.bin \
        --v3 platform/uv-k5-v3-py32f071/firmware_uvk5_v3.bin \
        --manual <Handleiding-Scout-porto-1.1.pdf> --out release/1.1

writes into --out:

    scout-firmware.zip           manifest.json + the two images + the manual + LICENSE and NOTICE
                                 (Apache-2.0 section 4: the licence goes with the binaries),
                                 byte-identical on a rebuild
    Handleiding-Scout-porto.pdf  the manual on its own
    scout-update.json            what ScoutFlasher reads first (sf/update.py parse_update)

Second call, after PyInstaller:

    python tools/make_release_assets.py --out release/1.1 --exe tools/scout-flasher/dist/ScoutFlasher.exe

copies the EXE to <out>/ScoutFlasher.exe (if it is not already that file) and adds its
SHA-256 to scout-update.json. --out then holds exactly the four release assets.

Standard library only (plus sf.manifest / sf.update from tools/scout-flasher, which are too).
"""

import argparse
import datetime
import hashlib
import io
import json
import os
import re
import shutil
import sys
import tempfile
import zipfile
from pathlib import Path

_FLASHER_DIR = Path(__file__).resolve().parent / "scout-flasher"
if str(_FLASHER_DIR) not in sys.path:
    sys.path.insert(0, str(_FLASHER_DIR))

from sf import update  # noqa: E402
from sf.manifest import MANIFEST_NAME, write_manifest  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent
LICENCE_NAMES = ("LICENSE", "NOTICE")   # repo root; checksummed in the manifest like the images

ZIP_NAME = "scout-firmware.zip"
MANUAL_NAME = "Handleiding-Scout-porto.pdf"
V1_NAME = "firmware_uvk5_v1.packed.bin"
V3_NAME = "firmware_uvk5_v3.bin"
UPDATE_NAME = update.UPDATE_NAME        # scout-update.json
EXE_NAME = update.EXE_NAME              # ScoutFlasher.exe
ASSETS = (EXE_NAME, ZIP_NAME, UPDATE_NAME, MANUAL_NAME)

ZIP_DATE = (2020, 1, 1, 0, 0, 0)        # fixed member timestamps: a rebuild is byte-identical
_COMMIT_RE = re.compile(r"[0-9a-f]{7,40}")   # a clean `git describe --always --abbrev=7 --exclude '*'`


def die(msg: str):
    print("make_release_assets: " + msg, file=sys.stderr)
    raise SystemExit(2)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _version(name: str, v: str) -> str:
    try:
        update.version_tuple(v)
    except ValueError:
        die("--{} {!r} is not a version (digits and dots, optional -suffix such as 1.1-rc1)".format(name, v))
    return v


def _input_file(name: str, p: str) -> Path:
    path = Path(p)
    if not path.is_file():
        die("--{}: no such file: {}".format(name, p))
    return path


def build_zip(files: dict) -> bytes:
    """files: member name -> bytes. Sorted members, fixed dates and attributes."""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        for name in sorted(files):
            zi = zipfile.ZipInfo(name, date_time=ZIP_DATE)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.create_system = 3                    # same bytes whatever OS builds it
            zi.external_attr = 0o100644 << 16       # regular file, rw-r--r--
            z.writestr(zi, files[name], compresslevel=9)
    return buf.getvalue()


def _manifest_bytes(out: Path, version: str, v1: bytes, v3: bytes, manual: bytes, licences: dict) -> bytes:
    """manifest.json via sf.manifest.write_manifest, written in a private folder inside out.

    Only the files written here are removed again, then the (empty) folder.
    """
    stage = Path(tempfile.mkdtemp(prefix=".stage-", dir=out))
    names = (V1_NAME, V3_NAME, MANUAL_NAME, MANIFEST_NAME, *licences)
    try:
        (stage / V1_NAME).write_bytes(v1)
        (stage / V3_NAME).write_bytes(v3)
        (stage / MANUAL_NAME).write_bytes(manual)
        for name, data in licences.items():
            (stage / name).write_bytes(data)
        write_manifest(stage, version, V1_NAME, V3_NAME, MANUAL_NAME, extra=tuple(licences))
        return (stage / MANIFEST_NAME).read_bytes()
    finally:
        for n in names:
            (stage / n).unlink(missing_ok=True)
        stage.rmdir()


def _read_licences(folder: Path) -> dict:
    """LICENSE and NOTICE of the repo, as bytes. A release without them is refused.

    Line ends are LF, as git stores the files: a checkout with autocrlf turns them into CRLF
    (LICENSE and NOTICE can even differ), and the zip must not depend on that."""
    found = {}
    for name in LICENCE_NAMES:
        path = folder / name
        if not path.is_file():
            die("{} not found in {}: the release must ship its licence".format(name, folder))
        found[name] = path.read_bytes().replace(b"\r\n", b"\n")
    return found


def _write_json(path: Path, data: dict) -> None:
    path.write_bytes((json.dumps(data, indent=2) + "\n").encode("utf-8"))   # UTF-8, LF, no BOM


def make_assets(out: Path, version: str, commit: str, date: str, flasher_version: str,
                min_flasher: str, v1: Path, v3: Path, manual: Path,
                licence_dir: Path = REPO_ROOT) -> dict:
    """Write scout-firmware.zip, the manual and scout-update.json (without "exe") into out.

    LICENSE and NOTICE from licence_dir (the repo root) go into the zip."""
    v1_bytes, v3_bytes, manual_bytes = v1.read_bytes(), v3.read_bytes(), manual.read_bytes()
    licences = _read_licences(licence_dir)
    out.mkdir(parents=True, exist_ok=True)
    manifest = _manifest_bytes(out, version, v1_bytes, v3_bytes, manual_bytes, licences)
    zip_bytes = build_zip({V1_NAME: v1_bytes, V3_NAME: v3_bytes, MANUAL_NAME: manual_bytes,
                           MANIFEST_NAME: manifest, **licences})
    (out / ZIP_NAME).write_bytes(zip_bytes)
    (out / MANUAL_NAME).write_bytes(manual_bytes)
    info = {
        "release": version,
        "commit": commit,
        "date": date,
        "flasher_version": flasher_version,
        "min_flasher": min_flasher,
        "firmware_zip": {"name": ZIP_NAME, "size": len(zip_bytes), "sha256": sha256(zip_bytes)},
    }
    _write_json(out / UPDATE_NAME, info)
    _check_update_json(out, info)
    return info


def add_exe(out: Path, exe: Path) -> dict:
    """Put the EXE at out/ScoutFlasher.exe and record its SHA-256 in scout-update.json."""
    json_path = out / UPDATE_NAME
    if not json_path.is_file():
        die("{} not found: run without --exe first".format(json_path))
    info = json.loads(json_path.read_text(encoding="utf-8"))
    target = out / EXE_NAME
    if not (target.exists() and os.path.samefile(exe, target)):
        shutil.copyfile(exe, target)
    info.pop("exe", None)
    info["exe"] = {"name": EXE_NAME, "sha256": sha256(target.read_bytes())}
    _write_json(json_path, info)
    _check_update_json(out, info)
    return info


def _check_update_json(out: Path, info: dict) -> None:
    """Read the json back exactly as ScoutFlasher does; it must agree with what was meant."""
    parsed = update.parse_update((out / UPDATE_NAME).read_text(encoding="utf-8"))
    zip_bytes = (out / ZIP_NAME).read_bytes()
    if (parsed.release, parsed.zip_name, parsed.zip_size, parsed.zip_sha256) != \
            (info["release"], ZIP_NAME, len(zip_bytes), sha256(zip_bytes)):
        die("{} does not describe {}".format(UPDATE_NAME, ZIP_NAME))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", required=True, help="release folder, e.g. release/1.1")
    ap.add_argument("--exe", help="second call: the built ScoutFlasher.exe")
    ap.add_argument("--version", help="release, e.g. 1.1 or 1.1-rc1")
    ap.add_argument("--commit", help="git describe --always --abbrev=7 --exclude '*' (clean)")
    ap.add_argument("--date", help="build date, yyyy-mm-dd")
    ap.add_argument("--flasher-version", help="FLASHER_VERSION of the EXE published with it")
    ap.add_argument("--min-flasher", help="oldest ScoutFlasher that may use this release")
    ap.add_argument("--v1", help="packed V1 image (K5TOOL -wrflash)")
    ap.add_argument("--v3", help="raw V3 image")
    ap.add_argument("--manual", help="manual PDF")
    a = ap.parse_args(argv)
    out = Path(a.out)

    if a.exe:
        info = add_exe(out, _input_file("exe", a.exe))
    else:
        missing = [n for n in ("version", "commit", "date", "flasher_version", "min_flasher", "v1", "v3", "manual")
                   if not getattr(a, n)]
        if missing:
            die("missing: " + ", ".join("--" + n.replace("_", "-") for n in missing))
        version = _version("version", a.version)
        flasher_version = _version("flasher-version", a.flasher_version)
        min_flasher = _version("min-flasher", a.min_flasher)
        if update.version_tuple(min_flasher) > update.version_tuple(flasher_version):
            die("--min-flasher {} is above --flasher-version {}: no published ScoutFlasher "
                "could use this release".format(min_flasher, flasher_version))
        if not _COMMIT_RE.fullmatch(a.commit):
            die("--commit {!r} is not a clean short commit hash (a release needs a clean tree)".format(a.commit))
        try:
            if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", a.date):
                raise ValueError
            datetime.date.fromisoformat(a.date)
        except ValueError:
            die("--date {!r} is not yyyy-mm-dd".format(a.date))
        info = make_assets(out, version, a.commit, a.date, flasher_version, min_flasher,
                           _input_file("v1", a.v1), _input_file("v3", a.v3), _input_file("manual", a.manual))

    print("Release {} (commit {}) in {}".format(info["release"], info["commit"], out))
    for name in ASSETS:
        p = out / name
        if p.is_file():
            print("  {:<28} {:>10,} bytes  sha256 {}".format(name, p.stat().st_size, sha256(p.read_bytes())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
