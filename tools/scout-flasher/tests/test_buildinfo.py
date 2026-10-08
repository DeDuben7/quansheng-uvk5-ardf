import importlib
import sys
import types

import pytest

import sf
from sf import buildinfo


@pytest.fixture
def fresh_buildinfo(monkeypatch):
    """Make the reload tests independent of a real, generated sf/_build_info.py.

    Importing buildinfo sets the attribute sf._build_info on the package, and
    `from sf import _build_info` looks at that attribute before sys.modules,
    so drop it first. On teardown restore everything (monkeypatch.undo()
    first) and reload, so buildinfo ends up as it was.
    """
    monkeypatch.delattr(sf, "_build_info", raising=False)
    yield
    monkeypatch.undo()
    importlib.reload(buildinfo)


def test_defaults_without_generated_module(monkeypatch, fresh_buildinfo):
    monkeypatch.setitem(sys.modules, "sf._build_info", None)   # import fails
    b = importlib.reload(buildinfo)
    assert b.COMMIT == "dev" and b.BUILD_DATE == ""
    assert b.label() == "ScoutFlasher {} · dev".format(sf.FLASHER_VERSION)


def test_generated_module_wins(monkeypatch, fresh_buildinfo):
    fake = types.ModuleType("sf._build_info")
    fake.COMMIT, fake.BUILD_DATE = "8b8060a", "2026-10-08"
    monkeypatch.setitem(sys.modules, "sf._build_info", fake)
    b = importlib.reload(buildinfo)
    assert b.COMMIT == "8b8060a" and b.BUILD_DATE == "2026-10-08"
    assert b.label().endswith("· 8b8060a")


def test_flasher_version_is_numeric():
    assert all(p.isdigit() for p in sf.FLASHER_VERSION.split("."))


def test_icon_paths_from_source_and_frozen(monkeypatch, tmp_path):
    from sf import paths
    assert paths.icon_ico() == paths._TOOL_DIR / "icon" / "scoutflasher.ico"
    assert paths.icon_png() == paths._TOOL_DIR / "icon" / "scoutflasher.png"
    assert paths.icon_ico().is_file() and paths.icon_png().is_file()
    monkeypatch.setattr(sys, "frozen", True, raising=False)
    monkeypatch.setattr(sys, "_MEIPASS", str(tmp_path), raising=False)
    assert paths.icon_ico() == tmp_path / "icon" / "scoutflasher.ico"
    assert paths.icon_png() == tmp_path / "icon" / "scoutflasher.png"
