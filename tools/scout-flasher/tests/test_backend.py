import tempfile
import threading

import pytest

from sf import backend, update
from sf.backend import SimBackend
from sf.manifest import sha256_file, verify


class Log:
    def __init__(self):
        self.lines = []

    def write(self, s):
        self.lines.append(s)


@pytest.fixture(autouse=True)
def fast_sim(monkeypatch):
    monkeypatch.setattr(backend, "SIM_FETCH_DELAY", 0.0)


def test_sim_ok_walks_all_steps():
    b = SimBackend(Log(), "ok")
    res = b.check_cable()
    assert res.state == "ok" and res.port == "COM9"
    beacon = b.wait_radio(res.port, threading.Event())
    assert beacon.kind == "V3"
    seen = []
    assert b.flash(res.port, beacon, lambda d, t: seen.append((d, t)), threading.Event())
    assert seen[-1] == (55, 55)


def test_sim_driver_fix_then_ok():
    b = SimBackend(Log(), "driver-fix")
    assert b.check_cable().state == "needs_fix"
    assert b.fix_driver() == 0
    assert b.check_cable().state == "ok"


def test_sim_driver_stuck_after_fix():
    b = SimBackend(Log(), "driver-stuck")
    assert b.check_cable().state == "needs_fix"
    b.fix_driver()
    res = b.check_cable()
    assert res.state == "driver_problem" and res.problem == 39 and res.memory_integrity


def test_sim_failures():
    assert SimBackend(Log(), "no-cable").check_cable().state == "not_found"
    b = SimBackend(Log(), "unknown-bl")
    assert b.wait_radio("COM9", threading.Event()).kind is None
    f = SimBackend(Log(), "flash-fail")
    assert not f.flash("COM9", None, lambda d, t: None, threading.Event())


def test_sim_no_radio_returns_on_cancel():
    cancel = threading.Event()
    threading.Timer(0.3, cancel.set).start()
    assert SimBackend(Log(), "no-radio").wait_radio("COM9", cancel) is None


def test_sim_prepare_ok_gives_a_verified_package(tmp_path):
    b = SimBackend(Log(), "ok", work_dir=tmp_path)
    pkg = b.prepare()
    assert pkg.info.release == "1.1" and pkg.manifest.version == "1.1"
    assert pkg.info.commit == "sim0000" and pkg.info.date == "2026-10-08"
    assert pkg.info.flasher_version == "1.1" and pkg.info.min_flasher == "1.1"
    names = sorted(p.name for p in pkg.dir.iterdir())
    assert names == sorted(["manifest.json", pkg.manifest.v1_file, pkg.manifest.v3_file, pkg.manifest.manual])
    assert verify(pkg.dir, pkg.manifest) == []


def test_sim_prepare_default_work_dir_and_log_argument(tmp_path, monkeypatch):
    monkeypatch.setattr(tempfile, "tempdir", str(tmp_path))    # mkdtemp() lands in tmp_path, not in %TEMP%
    lines = []
    pkg = SimBackend(Log(), "ok").prepare(lines.append)
    assert (pkg.dir / "manifest.json").is_file()
    assert tmp_path in pkg.dir.parents
    assert lines          # the line writer passed in is used


@pytest.mark.parametrize("scenario, kind", [("offline", "offline"), ("needs-new-exe", "too_old"),
                                            ("bad-download", "bad_download")])
def test_sim_prepare_failures(scenario, kind, tmp_path):
    with pytest.raises(update.UpdateError) as e:
        SimBackend(Log(), scenario, work_dir=tmp_path).prepare()
    assert e.value.kind == kind and e.value.message


def test_sim_newer_exe_advertises_1_2(tmp_path):
    pkg = SimBackend(Log(), "newer-exe", work_dir=tmp_path).prepare()
    assert pkg.info.flasher_version == "1.2"
    assert update.newer_exe_available(pkg.info, "1.1")


def test_sim_other_scenarios_do_not_advertise_a_new_exe(tmp_path):
    pkg = SimBackend(Log(), "flash-fail", work_dir=tmp_path).prepare()
    assert not update.newer_exe_available(pkg.info, "1.1")


def test_new_scenarios_are_listed():
    for s in ("offline", "needs-new-exe", "newer-exe", "bad-download"):
        assert s in backend.SCENARIOS


def test_real_prepare_remembers_package_and_flash_reads_from_it(tmp_path, monkeypatch):
    sim_pkg = SimBackend(Log(), "ok", work_dir=tmp_path / "sim").prepare()
    image = sim_pkg.dir / sim_pkg.manifest.v3_file
    image.write_bytes(b"IMAGE-V3")
    sim_pkg.manifest.sha256[sim_pkg.manifest.v3_file] = sha256_file(image)    # flash() re-verifies the package
    calls = []

    def fake_fetch_latest(dest, flasher_version, log, fetch=None):
        calls.append((dest, flasher_version))
        log("fetched")
        return sim_pkg

    monkeypatch.setattr(update, "fetch_latest", fake_fetch_latest)
    flashed = []
    monkeypatch.setattr(backend.flash, "flash_v3",
                        lambda port, image, progress, log, cancel, bl_ver: flashed.append((port, image, bl_ver)) or True)
    log = Log()
    b = backend.RealBackend(log, tmp_path / "work")
    assert b.prepare() is sim_pkg
    assert calls == [(tmp_path / "work", "1.1")]
    assert any("1.1" in line and "sim0000" in line for line in log.lines)       # release + commit logged
    assert any(sim_pkg.manifest.sha256[sim_pkg.manifest.v3_file] in line for line in log.lines)   # hashes logged
    assert b.flash("COM9", backend.Beacon("7.00.07", "V3"), lambda d, t: None, threading.Event())
    assert flashed == [("COM9", b"IMAGE-V3", "7.00")]


@pytest.mark.parametrize("kind, version, image_of, damage", [
    ("V3", "7.00.07", lambda m: m.v3_file, "tamper"),
    ("V1", "2.00.06", lambda m: m.v1_file, "tamper"),
    ("V3", "7.00.07", lambda m: m.v3_file, "delete"),
    ("V1", "2.00.06", lambda m: m.v1_file, "delete"),
    ("V3", "7.00.07", lambda m: m.manual, "tamper"),      # any file of the package counts
])
def test_real_flash_refuses_a_package_that_changed_after_prepare(tmp_path, monkeypatch, kind, version,
                                                                 image_of, damage):
    sim_pkg = SimBackend(Log(), "ok", work_dir=tmp_path / "sim").prepare()
    monkeypatch.setattr(update, "fetch_latest", lambda dest, flasher_version, log, fetch=None: sim_pkg)
    flashed = []
    monkeypatch.setattr(backend.flash, "flash_v3", lambda *a, **k: flashed.append(("v3", a)) or True)
    monkeypatch.setattr(backend.flash, "flash_v1", lambda *a, **k: flashed.append(("v1", a)) or True)
    log = Log()
    b = backend.RealBackend(log, tmp_path / "work")
    b.prepare()
    name = image_of(sim_pkg.manifest)
    if damage == "tamper":
        (sim_pkg.dir / name).write_bytes(b"TAMPERED")
    else:
        (sim_pkg.dir / name).unlink()
    with pytest.raises(backend.PackageDamaged):
        b.flash("COM9", backend.Beacon(version, kind), lambda d, t: None, threading.Event())
    assert flashed == []                                         # nothing reached the flasher
    assert any(name in line and "not flashing" in line for line in log.lines)


def test_real_flash_accepts_an_untouched_package_for_both_radios(tmp_path, monkeypatch):
    sim_pkg = SimBackend(Log(), "ok", work_dir=tmp_path / "sim").prepare()
    monkeypatch.setattr(update, "fetch_latest", lambda dest, flasher_version, log, fetch=None: sim_pkg)
    flashed = []
    monkeypatch.setattr(backend.flash, "flash_v3", lambda *a, **k: flashed.append("v3") or True)
    monkeypatch.setattr(backend.flash, "flash_v1", lambda *a, **k: flashed.append("v1") or True)
    b = backend.RealBackend(Log(), tmp_path / "work")
    b.prepare()
    assert b.flash("COM9", backend.Beacon("7.00.07", "V3"), lambda d, t: None, threading.Event())
    assert b.flash("COM9", backend.Beacon("2.00.06", "V1"), lambda d, t: None, threading.Event())
    assert flashed == ["v3", "v1"]


def test_real_prepare_passes_update_errors_through(tmp_path, monkeypatch):
    def offline(*a, **k):
        raise update.UpdateError("offline", "no network")
    monkeypatch.setattr(update, "fetch_latest", offline)
    with pytest.raises(update.UpdateError):
        backend.RealBackend(Log(), tmp_path).prepare()
