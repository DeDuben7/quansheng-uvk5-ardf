"""manifest.json inside scout-firmware.zip: the release's firmware, with checksums.

Online-only design: the EXE carries no firmware. The package is downloaded
from the latest GitHub release at every run; sf/update.fetch_latest verifies it
with verify() below, and RealBackend.flash verifies it again right before it
reads an image, so a damaged package never reaches a radio. Written by
tools/make_release_assets.py (called by tools/release.ps1).
"""

import hashlib
import json
from dataclasses import dataclass, field
from pathlib import Path

MANIFEST_NAME = "manifest.json"


@dataclass
class Manifest:
    version: str            # "1.0"
    v1_file: str            # packed image for K5TOOL -wrflash
    v3_file: str            # raw image for the ProtocolV2 flasher
    manual: str             # PDF name
    sha256: dict[str, str] = field(default_factory=dict)


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_manifest(fw_dir: Path, version: str, v1_file: str, v3_file: str, manual: str,
                   extra: tuple[str, ...] = ()) -> Manifest:
    """extra: further files of the package (LICENSE, NOTICE) that get a checksum too, so
    verify() covers them like the images; no other field names them."""
    m = Manifest(version, v1_file, v3_file, manual)
    m.sha256 = {name: sha256_file(fw_dir / name) for name in (v1_file, v3_file, manual, *extra)}
    (fw_dir / MANIFEST_NAME).write_text(json.dumps(m.__dict__, indent=2), encoding="utf-8")
    return m


def load_manifest(fw_dir: Path) -> Manifest:
    data = json.loads((fw_dir / MANIFEST_NAME).read_text(encoding="utf-8"))
    return Manifest(**data)


def verify(fw_dir: Path, m: Manifest) -> list[str]:
    """Names of the package's files that are missing or don't match their hash."""
    bad = []
    for name, digest in m.sha256.items():
        p = fw_dir / name
        if not p.is_file() or sha256_file(p) != digest:
            bad.append(name)
    return bad
