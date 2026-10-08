"""tools/make_release_assets.py: the four fixed-name GitHub release assets."""

import hashlib
import importlib.util
import json
import os
import zipfile
from pathlib import Path

import pytest

from sf import update

SCRIPT = Path(__file__).resolve().parents[2] / "make_release_assets.py"   # tools/make_release_assets.py
REPO = SCRIPT.parent.parent

ZIP_MEMBERS = ["Handleiding-Scout-porto.pdf", "LICENSE", "NOTICE", "firmware_uvk5_v1.packed.bin",
               "firmware_uvk5_v3.bin", "manifest.json"]
ASSETS = ["Handleiding-Scout-porto.pdf", "ScoutFlasher.exe", "scout-firmware.zip", "scout-update.json"]


@pytest.fixture(scope="module")
def mra():
    spec = importlib.util.spec_from_file_location("make_release_assets", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


@pytest.fixture
def inputs(tmp_path):
    src = tmp_path / "in"
    src.mkdir()
    v1 = src / "firmware_uvk5_v1.packed.bin"
    v3 = src / "firmware_uvk5_v3.bin"
    pdf = src / "Handleiding-Scout-porto-1.1.pdf"
    v1.write_bytes(b"V1" * 300)
    v3.write_bytes(b"V3" * 300)
    pdf.write_bytes(b"%PDF-1.4 manual")
    return v1, v3, pdf


def run(mra, out, inputs, version="1.1", commit="8b8060a", min_flasher="1.1", flasher_version="1.1"):
    v1, v3, pdf = inputs
    return mra.main(["--version", version, "--commit", commit, "--date", "2026-10-08",
                     "--flasher-version", flasher_version, "--min-flasher", min_flasher,
                     "--v1", str(v1), "--v3", str(v3), "--manual", str(pdf), "--out", str(out)])


def fetch_from(root: Path):
    """A fake fetch serving the files of an output folder (like GitHub's latest/download/)."""
    def fetch(url, timeout):
        p = root / url.rsplit("/", 1)[-1]
        if not p.is_file():
            raise update.UpdateError("offline", "404 " + p.name)
        return p.read_bytes()
    return fetch


class Log(list):
    def __call__(self, line):
        self.append(line)


def test_assets_round_trip_through_fetch_latest(mra, tmp_path, inputs):
    out = tmp_path / "out"
    assert run(mra, out, inputs) == 0
    assert sorted(p.name for p in out.iterdir()) == \
        ["Handleiding-Scout-porto.pdf", "scout-firmware.zip", "scout-update.json"]
    assert (out / "Handleiding-Scout-porto.pdf").read_bytes() == inputs[2].read_bytes()

    pkg = update.fetch_latest(tmp_path / "dl", "1.1", Log(), fetch=fetch_from(out))
    assert pkg.info.release == "1.1" and pkg.manifest.version == "1.1"
    assert pkg.info.commit == "8b8060a" and pkg.info.date == "2026-10-08"
    assert (pkg.info.flasher_version, pkg.info.min_flasher) == ("1.1", "1.1")
    assert (pkg.manifest.v1_file, pkg.manifest.v3_file, pkg.manifest.manual) == \
        ("firmware_uvk5_v1.packed.bin", "firmware_uvk5_v3.bin", "Handleiding-Scout-porto.pdf")
    assert (pkg.dir / pkg.manifest.v1_file).read_bytes() == inputs[0].read_bytes()
    assert (pkg.dir / pkg.manifest.v3_file).read_bytes() == inputs[1].read_bytes()
    assert (pkg.dir / pkg.manifest.manual).read_bytes() == inputs[2].read_bytes()
    # Apache-2.0 section 4: the licence travels with the binaries; the manifest checksums it too.
    for name in ("LICENSE", "NOTICE"):
        committed = (REPO / name).read_bytes().replace(b"\r\n", b"\n")     # as git stores it
        assert (pkg.dir / name).read_bytes() == committed
        assert pkg.manifest.sha256[name] == hashlib.sha256(committed).hexdigest()


def test_two_runs_give_byte_identical_assets(mra, tmp_path, inputs):
    run(mra, tmp_path / "a", inputs)
    for f in inputs:                       # newer mtimes must not leak into the zip
        os.utime(f, (1_900_000_000, 1_900_000_000))
    run(mra, tmp_path / "b", inputs)
    for name in ("scout-firmware.zip", "scout-update.json", "Handleiding-Scout-porto.pdf"):
        assert (tmp_path / "a" / name).read_bytes() == (tmp_path / "b" / name).read_bytes(), name


def test_zip_layout_sorted_fixed_timestamps(mra, tmp_path, inputs):
    out = tmp_path / "out"
    run(mra, out, inputs)
    with zipfile.ZipFile(out / "scout-firmware.zip") as z:
        infos = z.infolist()
        assert [i.filename for i in infos] == ZIP_MEMBERS
        assert all(i.date_time == (2020, 1, 1, 0, 0, 0) for i in infos)
        assert all(i.compress_type == zipfile.ZIP_DEFLATED for i in infos)
        manifest = json.loads(z.read("manifest.json"))
    assert manifest["version"] == "1.1"
    assert manifest["sha256"]["firmware_uvk5_v3.bin"] == hashlib.sha256(inputs[1].read_bytes()).hexdigest()


def test_tampered_licence_in_the_zip_is_refused(mra, tmp_path, inputs):
    # The zip's own hash is made to match, so only the manifest's checksum can catch it.
    out = tmp_path / "out"
    run(mra, out, inputs)
    with zipfile.ZipFile(out / "scout-firmware.zip") as z:
        members = {n: z.read(n) for n in z.namelist()}
    members["LICENSE"] = b"Not the Apache License"
    data = mra.build_zip(members)
    (out / "scout-firmware.zip").write_bytes(data)
    info = json.loads((out / "scout-update.json").read_text(encoding="utf-8"))
    info["firmware_zip"].update(size=len(data), sha256=hashlib.sha256(data).hexdigest())
    (out / "scout-update.json").write_text(json.dumps(info), encoding="utf-8")
    with pytest.raises(update.UpdateError) as e:
        update.fetch_latest(tmp_path / "dl", "1.1", Log(), fetch=fetch_from(out))
    assert e.value.kind == "bad_download" and "LICENSE" in e.value.message
    assert not (tmp_path / "dl" / "1.1").exists()


def test_a_release_without_its_licence_is_refused(mra, tmp_path, inputs):
    bare = tmp_path / "bare"
    bare.mkdir()
    (bare / "LICENSE").write_bytes(b"licence")           # NOTICE missing
    out = tmp_path / "out"
    v1, v3, pdf = inputs
    with pytest.raises(SystemExit) as e:
        mra.make_assets(out, "1.1", "8b8060a", "2026-10-08", "1.1", "1.1", v1, v3, pdf, licence_dir=bare)
    assert e.value.code not in (0, None)
    assert not out.exists()


def test_update_json_schema(mra, tmp_path, inputs):
    out = tmp_path / "out"
    run(mra, out, inputs)
    data = (out / "scout-update.json").read_bytes()
    info = json.loads(data)
    assert list(info) == ["release", "commit", "date", "flasher_version", "min_flasher", "firmware_zip"]
    zip_bytes = (out / "scout-firmware.zip").read_bytes()
    assert info["firmware_zip"] == {"name": "scout-firmware.zip", "size": len(zip_bytes),
                                    "sha256": hashlib.sha256(zip_bytes).hexdigest()}
    assert not data.startswith(b"\xef\xbb\xbf") and b"\r" not in data


def test_exe_call_fills_in_the_exe_hash(mra, tmp_path, inputs):
    out = tmp_path / "out"
    run(mra, out, inputs)
    exe = out / "ScoutFlasher.exe"
    exe.write_bytes(b"MZ fake exe")
    assert mra.main(["--out", str(out), "--exe", str(exe)]) == 0
    info = json.loads((out / "scout-update.json").read_text(encoding="utf-8"))
    assert list(info) == ["release", "commit", "date", "flasher_version", "min_flasher", "firmware_zip", "exe"]
    assert info["exe"] == {"name": "ScoutFlasher.exe", "sha256": hashlib.sha256(b"MZ fake exe").hexdigest()}
    assert sorted(p.name for p in out.iterdir()) == ASSETS
    update.parse_update((out / "scout-update.json").read_text(encoding="utf-8"))   # still what the EXE reads


def test_exe_from_elsewhere_is_copied_to_the_fixed_name(mra, tmp_path, inputs):
    out = tmp_path / "out"
    run(mra, out, inputs)
    built = tmp_path / "dist" / "ScoutFlasher.exe"
    built.parent.mkdir()
    built.write_bytes(b"MZ built")
    assert mra.main(["--out", str(out), "--exe", str(built)]) == 0
    assert (out / "ScoutFlasher.exe").read_bytes() == b"MZ built"
    info = json.loads((out / "scout-update.json").read_text(encoding="utf-8"))
    assert info["exe"]["sha256"] == hashlib.sha256(b"MZ built").hexdigest()


def test_prerelease_suffix_round_trips(mra, tmp_path, inputs):
    out = tmp_path / "out"
    assert run(mra, out, inputs, version="1.1-rc1") == 0
    pkg = update.fetch_latest(tmp_path / "dl", "1.1", Log(), fetch=fetch_from(out))
    assert pkg.info.release == "1.1-rc1" and pkg.manifest.version == "1.1-rc1"
    assert pkg.dir == tmp_path / "dl" / "1.1-rc1"


@pytest.mark.parametrize("kwargs", [
    {"commit": "8b8060a-dirty"},
    {"commit": "v1.0-3-g8b8060a"},
    {"version": "1.1-"},
    {"version": "../1.1"},
    {"min_flasher": "1.x"},
    {"min_flasher": "1.2", "flasher_version": "1.1"},     # no published EXE could use it
], ids=["dirty", "describe-tag", "empty-suffix", "path", "bad-min", "min-above-flasher"])
def test_bad_arguments_write_nothing(mra, tmp_path, inputs, kwargs):
    out = tmp_path / "out"
    with pytest.raises(SystemExit) as e:
        run(mra, out, inputs, **kwargs)
    assert e.value.code not in (0, None)
    assert not out.exists() or not any(out.iterdir())


def test_exe_call_needs_the_first_call(mra, tmp_path):
    out = tmp_path / "out"
    out.mkdir()
    exe = tmp_path / "ScoutFlasher.exe"
    exe.write_bytes(b"MZ")
    with pytest.raises(SystemExit) as e:
        mra.main(["--out", str(out), "--exe", str(exe)])
    assert e.value.code not in (0, None)
    assert not (out / "scout-update.json").exists()
