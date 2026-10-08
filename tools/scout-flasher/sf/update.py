"""Online-only update: fetch the latest firmware package from GitHub and verify it.

The flasher carries no firmware. Every run downloads two files from the latest
release (``releases/latest/download/``):

    scout-update.json     small description: release, commit, min flasher, zip name/size/SHA-256
    scout-firmware.zip    manifest.json + the V1 and V3 images + the manual (PDF) + LICENSE and NOTICE

Nothing is trusted until it checks out: the zip must match the size and SHA-256
from scout-update.json, its members must be plain file names, and the manifest
inside must verify every file and name the same release. Only then is a
``Package`` returned. A failed package never stays on disk.

Failures are ``UpdateError`` with a ``kind`` the UI maps to a Dutch message:
    offline        no network, or the release/file is not there (HTTP 404)
    too_old        this ScoutFlasher is older than the release's min_flasher
    bad_info       scout-update.json is not usable
    bad_download   the zip is damaged, tampered with or inconsistent
"""

import hashlib
import http.client
import io
import json
import os
import re
import shutil
import ssl
import urllib.request
import zipfile
import zlib
from dataclasses import dataclass
from pathlib import Path

try:
    import certifi      # imported on purpose: PyInstaller's certifi hook then bundles cacert.pem
except ImportError:     # source runs without it: the Windows certificate store alone is used
    certifi = None

from sf.manifest import Manifest, load_manifest, verify

DEFAULT_BASE = "https://github.com/DeDuben7/quansheng-uvk5-ardf/releases/latest/download/"
UPDATE_NAME = "scout-update.json"
EXE_NAME = "ScoutFlasher.exe"
RELEASES_PAGE = "https://github.com/DeDuben7/quansheng-uvk5-ardf/releases/latest"
ENV_BASE = "SCOUTFLASHER_UPDATE_URL"

INFO_TIMEOUT = 10        # seconds, scout-update.json
ZIP_TIMEOUT = 60         # seconds, scout-firmware.zip
MAX_BYTES = 32 * 1024 * 1024        # most we read from one response
MAX_UNPACKED = 64 * 1024 * 1024     # most a zip may unpack to

KINDS = ("offline", "too_old", "bad_info", "bad_download")


class UpdateError(Exception):
    """kind is one of KINDS; str(e) reads "kind: message"."""

    def __init__(self, kind: str, message: str):
        if kind not in KINDS:
            raise ValueError("unknown UpdateError kind {!r}".format(kind))
        super().__init__(message)
        self.kind = kind
        self.message = message

    def __str__(self) -> str:
        return "{}: {}".format(self.kind, self.message)


@dataclass(frozen=True)
class UpdateInfo:
    release: str            # "1.1" (or "1.1-rc1"), names the firmware package
    commit: str             # git commit the firmware was built from
    date: str
    flasher_version: str    # newest ScoutFlasher.exe published with this release
    min_flasher: str        # oldest ScoutFlasher that may use this release
    zip_name: str
    zip_size: int
    zip_sha256: str


@dataclass(frozen=True)
class Package:
    info: UpdateInfo
    dir: Path               # folder with manifest.json, the two images, the PDF, LICENSE and NOTICE
    manifest: Manifest


# ---- URLs and versions ------------------------------------------------------

def base_url() -> str:
    """SCOUTFLASHER_UPDATE_URL (tests, staging) or the latest GitHub release."""
    url = os.environ.get(ENV_BASE, "").strip() or DEFAULT_BASE
    return url if url.endswith("/") else url + "/"


def exe_url() -> str:
    return base_url() + EXE_NAME


# Dot-separated digits, optionally with a pre-release suffix such as "-rc1" or "-rc.1".
_VERSION_RE = re.compile(r"[0-9]+(\.[0-9]+)*(-[0-9A-Za-z]+(\.[0-9A-Za-z]+)*)?")


def version_tuple(v: str) -> tuple[int, ...]:
    """"1.10" -> (1, 10); "1.1-rc1" -> (1, 1): a "-..." suffix is ignored for comparison.
    Raises ValueError unless dot-separated digits with an optional suffix."""
    if not isinstance(v, str) or not _VERSION_RE.fullmatch(v):
        raise ValueError("not a version: {!r}".format(v))
    return tuple(int(p) for p in v.split("-", 1)[0].split("."))


def newer_exe_available(info: UpdateInfo, flasher_version: str) -> bool:
    return version_tuple(info.flasher_version) > version_tuple(flasher_version)


# ---- scout-update.json ------------------------------------------------------

def _plain_name(name: str) -> bool:
    """A bare file name: no folders, no drive, no '..'."""
    return bool(name) and not any(c in name for c in "/\\:") and ".." not in name


def parse_update(text: str) -> UpdateInfo:
    """Parse scout-update.json. Any defect raises UpdateError("bad_info")."""
    def bad(msg):
        return UpdateError("bad_info", "{} is not usable: {}".format(UPDATE_NAME, msg))

    try:
        data = json.loads(text)
    except ValueError as e:
        raise bad("not valid JSON ({})".format(e)) from e
    if not isinstance(data, dict):
        raise bad("not a JSON object")

    def field(d, key):
        v = d.get(key)
        if not isinstance(v, str) or not v:
            raise bad("missing or empty '{}'".format(key))
        return v

    zip_info = data.get("firmware_zip")
    if not isinstance(zip_info, dict):
        raise bad("missing 'firmware_zip'")
    size = zip_info.get("size")
    if not isinstance(size, int) or isinstance(size, bool) or size < 0:
        raise bad("'firmware_zip.size' is not a number")
    sha = field(zip_info, "sha256")
    if not re.fullmatch(r"[0-9a-fA-F]{64}", sha):
        raise bad("'firmware_zip.sha256' is not a SHA-256")
    zip_name = field(zip_info, "name")
    if not _plain_name(zip_name):
        raise bad("'firmware_zip.name' is not a plain file name: {!r}".format(zip_name))

    info = UpdateInfo(
        release=field(data, "release"), commit=field(data, "commit"), date=field(data, "date"),
        flasher_version=field(data, "flasher_version"), min_flasher=field(data, "min_flasher"),
        zip_name=zip_name, zip_size=size, zip_sha256=sha.lower())
    for key in ("release", "flasher_version", "min_flasher"):
        try:
            version_tuple(getattr(info, key))
        except ValueError as e:
            raise bad("'{}' is not a version ({})".format(key, e)) from e
    return info


# ---- download ---------------------------------------------------------------

def _ssl_context() -> ssl.SSLContext:
    """TLS settings for the downloads: certificates and host names are always checked,
    but a PC that Python's defaults cannot serve still gets through.

    - the Windows certificate store (create_default_context loads it), plus certifi's
      bundle on top, because Windows fetches a missing root certificate only when its
      own programs ask for it, so a fresh PC may not have the one GitHub needs yet;
    - without Python 3.13's VERIFY_X509_STRICT, which rejects the root certificates of
      some antivirus programs that scan HTTPS traffic (they are in the Windows store,
      but fail the extra strict checks).
    check_hostname and CERT_REQUIRED stay on.
    """
    ctx = ssl.create_default_context()
    if certifi is not None:
        try:
            ctx.load_verify_locations(cafile=certifi.where())
        except OSError:                 # includes ssl.SSLError
            pass                        # bundle missing or unreadable: the system store still works
    ctx.verify_flags &= ~ssl.VERIFY_X509_STRICT
    return ctx


def _http_get(url: str, timeout: float) -> bytes:
    """GET url (follows GitHub's 302 to objects.githubusercontent.com), at most MAX_BYTES."""
    req = urllib.request.Request(url, headers={"User-Agent": "ScoutFlasher"})
    with urllib.request.urlopen(req, timeout=timeout, context=_ssl_context()) as resp:
        return resp.read(MAX_BYTES)


def _logged(log, err: UpdateError) -> UpdateError:
    """Write the failure to the run log (the only place that formats it); returns err to raise."""
    log("Update failed ({}): {}".format(err.kind, err.message))
    return err


def _fail(log, kind: str, message: str) -> UpdateError:
    return _logged(log, UpdateError(kind, message))


def _get(fetch, url: str, timeout: float, log) -> bytes:
    """fetch(url, timeout) with every network failure turned into UpdateError("offline")."""
    log("GET {} (timeout {} s)".format(url, timeout))
    try:
        data = fetch(url, timeout)
    except UpdateError as e:
        raise _logged(log, e)
    except (OSError, http.client.HTTPException, ValueError) as e:
        # URLError, HTTPError (incl. 404), socket.timeout and TimeoutError are all OSErrors;
        # ValueError is urllib refusing a URL without a scheme (e.g. a mistyped SCOUTFLASHER_UPDATE_URL).
        raise _fail(log, "offline", "cannot get {}: {}: {}".format(url, type(e).__name__, e)) from e
    log("  received {} bytes".format(len(data)))
    return data


def _unpack(data: bytes, target: Path, log) -> None:
    """Unpack the zip flat into target (which must not exist). Refuses unsafe member names."""
    try:
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            members = z.infolist()
            for m in members:
                if not _plain_name(m.filename):
                    raise _fail(log, "bad_download", "unsafe name in package: {!r}".format(m.filename))
            total = sum(m.file_size for m in members)
            if total > MAX_UNPACKED:
                raise _fail(log, "bad_download", "package unpacks to {} bytes, too large".format(total))
            target.mkdir(parents=True)
            for m in members:
                (target / m.filename).write_bytes(z.read(m))    # z.read checks the CRC
                log("  unpacked {} ({} bytes)".format(m.filename, m.file_size))
    except (zipfile.BadZipFile, zlib.error, EOFError, NotImplementedError, RuntimeError) as e:
        raise _fail(log, "bad_download", "package is not a valid zip: {}: {}".format(type(e).__name__, e)) from e
    except OSError as e:
        raise _fail(log, "bad_download", "cannot unpack package to {}: {}".format(target, e)) from e


def _manifest_well_typed(m: Manifest) -> bool:
    """Strings where strings belong, and every file name in it a plain name (it came out of a JSON file)."""
    names = (m.v1_file, m.v3_file, m.manual)
    return (isinstance(m.version, str)
            and all(isinstance(n, str) and _plain_name(n) for n in names)
            and isinstance(m.sha256, dict)
            and all(isinstance(k, str) and _plain_name(k) and isinstance(v, str) for k, v in m.sha256.items()))


def _check_package(target: Path, info: UpdateInfo, log) -> Manifest:
    """The unpacked folder must be self-consistent and match the release."""
    try:
        m = load_manifest(target)
    except (OSError, ValueError, TypeError) as e:
        raise _fail(log, "bad_download", "package manifest unreadable: {}: {}".format(type(e).__name__, e)) from e
    log("Manifest: version {!r}, V1 {!r}, V3 {!r}, manual {!r}".format(m.version, m.v1_file, m.v3_file, m.manual))
    if not _manifest_well_typed(m):
        raise _fail(log, "bad_download", "package manifest has values of the wrong type or unsafe file names")
    unchecked = [n for n in (m.v1_file, m.v3_file, m.manual) if n not in m.sha256]
    if unchecked:
        raise _fail(log, "bad_download", "manifest does not checksum: {}".format(", ".join(map(repr, unchecked))))
    try:
        bad = verify(target, m)
    except OSError as e:
        raise _fail(log, "bad_download", "cannot check package files: {}: {}".format(type(e).__name__, e)) from e
    if bad:
        raise _fail(log, "bad_download", "files missing or damaged: {}".format(", ".join(bad)))
    if m.version != info.release:
        raise _fail(log, "bad_download", "manifest is version {}, release is {}".format(m.version, info.release))
    return m


def fetch_latest(dest: Path, flasher_version: str, log, fetch=_http_get) -> Package:
    """Download, verify and unpack the latest release into dest/<release>.

    flasher_version is this ScoutFlasher's own version. fetch(url, timeout) -> bytes
    is injectable for tests. Raises UpdateError; a failed package is not left on disk.
    A malformed flasher_version raises ValueError.
    """
    mine = version_tuple(flasher_version)      # ValueError = caller bug, not an update problem
    base = base_url()
    log("Update source: {}".format(base))

    raw = _get(fetch, base + UPDATE_NAME, INFO_TIMEOUT, log)
    try:
        info = parse_update(raw.decode("utf-8-sig"))
    except UnicodeDecodeError as e:
        raise _fail(log, "bad_info", "{} is not UTF-8 text".format(UPDATE_NAME)) from e
    except UpdateError as e:
        raise _logged(log, e)
    log("Release {} (commit {}, {}); flasher {} (needs >= {}); zip {} {} bytes sha256 {}".format(
        info.release, info.commit, info.date, info.flasher_version, info.min_flasher,
        info.zip_name, info.zip_size, info.zip_sha256))

    if version_tuple(info.min_flasher) > mine:
        raise _fail(log, "too_old", "release {} needs ScoutFlasher {} or newer, this is {}; "
                    "download the new {} from {}".format(info.release, info.min_flasher,
                                                         flasher_version, EXE_NAME, RELEASES_PAGE))

    data = _get(fetch, base + info.zip_name, ZIP_TIMEOUT, log)
    digest = hashlib.sha256(data).hexdigest()
    log("  sha256 {}".format(digest))
    if len(data) != info.zip_size:
        raise _fail(log, "bad_download", "{} is {} bytes, expected {}".format(info.zip_name, len(data), info.zip_size))
    if digest != info.zip_sha256:
        raise _fail(log, "bad_download", "{} sha256 {} does not match {}".format(info.zip_name, digest, info.zip_sha256))

    # target is rmtree'd below: it must be exactly dest/<release>, a direct child of dest
    # (no "..", no absolute path, no junction or symlink leading elsewhere).
    target = Path(dest) / info.release
    try:
        inside = target.name == info.release and target.resolve().parent == Path(dest).resolve()
    except (OSError, RuntimeError) as e:
        raise _fail(log, "bad_info", "release {!r} gives no usable folder: {}".format(info.release, e)) from e
    if not inside:
        raise _fail(log, "bad_info", "release {!r} is not a plain folder name in {}".format(info.release, dest))
    try:
        if target.exists():
            shutil.rmtree(target)
    except OSError as e:
        raise _fail(log, "bad_download", "cannot clear old package folder {}: {}".format(target, e)) from e
    try:
        _unpack(data, target, log)
        manifest = _check_package(target, info, log)
    except UpdateError:
        shutil.rmtree(target, ignore_errors=True)
        raise
    log("Package {} verified in {}".format(info.release, target))
    return Package(info, target, manifest)
