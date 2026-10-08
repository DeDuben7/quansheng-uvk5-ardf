import dataclasses
import hashlib
import io
import json
import os
import ssl
import threading
import types
import zipfile
from contextlib import contextmanager
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import pytest

from sf import update
from sf.manifest import write_manifest


def make_release(root: Path, release="1.1", min_flasher="1.1", flasher_version="1.1",
                 manifest_version=None, corrupt_zip=False, tamper_member=False):
    """Writes scout-update.json + scout-firmware.zip into root, like release.ps1."""
    stage = root / "stage"
    stage.mkdir(parents=True)
    (stage / "firmware_uvk5_v1.packed.bin").write_bytes(b"V1" * 100)
    (stage / "firmware_uvk5_v3.bin").write_bytes(b"V3" * 100)
    (stage / "Handleiding-Scout-porto.pdf").write_bytes(b"%PDF-1.4 test")
    write_manifest(stage, manifest_version or release, "firmware_uvk5_v1.packed.bin",
                   "firmware_uvk5_v3.bin", "Handleiding-Scout-porto.pdf")
    if tamper_member:
        (stage / "firmware_uvk5_v3.bin").write_bytes(b"EVIL")
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(stage.iterdir()):
            z.write(f, f.name)
    data = buf.getvalue()
    if corrupt_zip:
        data = data[: len(data) // 2]
    (root / "scout-firmware.zip").write_bytes(data)
    info = {"release": release, "commit": "abc1234", "date": "2026-10-08",
            "flasher_version": flasher_version, "min_flasher": min_flasher,
            "firmware_zip": {"name": "scout-firmware.zip", "size": len(data),
                             "sha256": hashlib.sha256(data).hexdigest()},
            "exe": {"name": "ScoutFlasher.exe", "sha256": "0" * 64}}
    (root / "scout-update.json").write_text(json.dumps(info), encoding="utf-8")
    return root


def fake_fetch(root: Path):
    def fetch(url, timeout):
        name = url.rsplit("/", 1)[-1]
        p = root / name
        if not p.exists():
            raise update.UpdateError("offline", "404 " + name)
        return p.read_bytes()
    return fetch


def offline_fetch(url, timeout):
    raise OSError("getaddrinfo failed")


class Log(list):
    def __call__(self, line):
        self.append(line)


def test_version_tuple_numeric():
    assert update.version_tuple("1.10") > update.version_tuple("1.9")
    assert update.version_tuple("2") == (2,)
    with pytest.raises(ValueError):
        update.version_tuple("1.x")


@pytest.mark.parametrize("v, expected", [
    ("1.1-rc1", (1, 1)),
    ("1.1-rc.1", (1, 1)),
    ("2.0.3-beta2", (2, 0, 3)),
    ("1.10-RC1", (1, 10)),
])
def test_version_tuple_ignores_a_suffix(v, expected):
    assert update.version_tuple(v) == expected


@pytest.mark.parametrize("v", ["1.1-", "1.1-..", "1.1-a/b", "..", "", "1.1-rc..1", "1.1-rc1-", "-rc1",
                               "1.1-rc 1", "1.1\n", "1..1", ".1", "1.", "1.1-r\u00e9", "\u0661.1", None, 1.1])
def test_version_tuple_rejects(v):
    with pytest.raises(ValueError):
        update.version_tuple(v)


def test_parse_update_accepts_a_prerelease(tmp_path):
    rel = make_release(tmp_path / "srv", release="1.1-rc1")
    info = update.parse_update((rel / "scout-update.json").read_text(encoding="utf-8"))
    assert info.release == "1.1-rc1"


def test_fetch_latest_prerelease_package(tmp_path):
    rel = make_release(tmp_path / "srv", release="1.1-rc1")
    pkg = update.fetch_latest(tmp_path / "dl", "1.1", Log(), fetch=fake_fetch(rel))
    assert pkg.info.release == "1.1-rc1" and pkg.dir == tmp_path / "dl" / "1.1-rc1"


@pytest.mark.parametrize("release", ["1.1-..", "1.1-a/b", "..", "../victim", "1.1/../../victim"])
def test_hostile_release_in_update_json_is_bad_info(tmp_path, release):
    rel = make_release(tmp_path / "srv")
    info = json.loads((rel / "scout-update.json").read_text(encoding="utf-8"))
    info["release"] = release
    (rel / "scout-update.json").write_text(json.dumps(info), encoding="utf-8")
    victim = tmp_path / "victim"
    victim.mkdir()
    (victim / "keep.txt").write_text("keep", encoding="utf-8")
    dest = tmp_path / "dl"
    with pytest.raises(update.UpdateError) as e:
        update.fetch_latest(dest, "1.1", Log(), fetch=fake_fetch(rel))
    assert e.value.kind == "bad_info"
    assert (victim / "keep.txt").read_text(encoding="utf-8") == "keep"


# Every hostile value points inside tmp_path ("ABS" = the victim's absolute path), so even a
# broken guard could only ever delete test files.
@pytest.mark.parametrize("release", ["..", ".", "", "../victim", "..\\victim", "x/../../victim", "ABS"])
def test_guard_never_deletes_outside_dest(tmp_path, monkeypatch, release):
    victim = tmp_path / "victim"
    victim.mkdir()
    (victim / "keep.txt").write_text("keep", encoding="utf-8")
    if release == "ABS":
        release = str(victim)
    # Get past parse_update (which already refuses these) to test the guard in fetch_latest itself.
    real_parse = update.parse_update
    monkeypatch.setattr(update, "parse_update",
                        lambda text: dataclasses.replace(real_parse(text), release=release))
    dest = tmp_path / "dl"
    dest.mkdir()
    (dest / "other.txt").write_text("keep", encoding="utf-8")
    rel = make_release(tmp_path / "srv")
    log = Log()
    with pytest.raises(update.UpdateError) as e:
        update.fetch_latest(dest, "1.1", log, fetch=fake_fetch(rel))
    assert e.value.kind == "bad_info"
    assert (victim / "keep.txt").read_text(encoding="utf-8") == "keep"
    assert (dest / "other.txt").read_text(encoding="utf-8") == "keep"
    assert sorted(p.name for p in tmp_path.iterdir()) == ["dl", "srv", "victim"]
    assert any("Update failed (bad_info)" in line for line in log)


def test_guard_refuses_a_junction_out_of_dest(tmp_path, monkeypatch):
    # dest/1.1 is a junction to a folder outside dest: never rmtree through it.
    _winapi = pytest.importorskip("_winapi")
    if not hasattr(_winapi, "CreateJunction"):
        pytest.skip("no junctions on this platform")
    victim = tmp_path / "victim"
    victim.mkdir()
    (victim / "keep.txt").write_text("keep", encoding="utf-8")
    dest = tmp_path / "dl"
    dest.mkdir()
    _winapi.CreateJunction(str(victim), str(dest / "1.1"))
    try:
        rel = make_release(tmp_path / "srv")
        with pytest.raises(update.UpdateError) as e:
            update.fetch_latest(dest, "1.1", Log(), fetch=fake_fetch(rel))
        assert e.value.kind == "bad_info"
        assert (victim / "keep.txt").read_text(encoding="utf-8") == "keep"
    finally:
        os.rmdir(dest / "1.1")      # removes the junction itself, never its target


def test_base_url_env_override(monkeypatch):
    monkeypatch.setenv("SCOUTFLASHER_UPDATE_URL", "http://127.0.0.1:9/rel/")
    assert update.base_url() == "http://127.0.0.1:9/rel/"
    assert update.exe_url() == "http://127.0.0.1:9/rel/ScoutFlasher.exe"
    monkeypatch.delenv("SCOUTFLASHER_UPDATE_URL")
    assert update.base_url() == update.DEFAULT_BASE


def test_fetch_latest_ok(tmp_path):
    rel = make_release(tmp_path / "srv")
    pkg = update.fetch_latest(tmp_path / "dl", "1.1", Log(), fetch=fake_fetch(rel))
    assert pkg.info.release == "1.1" and pkg.manifest.version == "1.1"
    assert (pkg.dir / "firmware_uvk5_v3.bin").read_bytes() == b"V3" * 100


def test_older_online_release_is_still_used(tmp_path):
    # Rollback = an older release marked Latest on GitHub.
    rel = make_release(tmp_path / "srv", release="1.0", min_flasher="1.0", flasher_version="1.0")
    pkg = update.fetch_latest(tmp_path / "dl", "1.1", Log(), fetch=fake_fetch(rel))
    assert pkg.info.release == "1.0"


def test_offline(tmp_path):
    with pytest.raises(update.UpdateError) as e:
        update.fetch_latest(tmp_path / "dl", "1.1", Log(), fetch=offline_fetch)
    assert e.value.kind == "offline"


def test_too_old_flasher(tmp_path):
    rel = make_release(tmp_path / "srv", release="1.3", min_flasher="1.2", flasher_version="1.2")
    with pytest.raises(update.UpdateError) as e:
        update.fetch_latest(tmp_path / "dl", "1.1", Log(), fetch=fake_fetch(rel))
    assert e.value.kind == "too_old"


def test_sha_mismatch(tmp_path):
    rel = make_release(tmp_path / "srv")
    (rel / "scout-firmware.zip").write_bytes((rel / "scout-firmware.zip").read_bytes()[:-1] + b"X")
    with pytest.raises(update.UpdateError) as e:
        update.fetch_latest(tmp_path / "dl", "1.1", Log(), fetch=fake_fetch(rel))
    assert e.value.kind == "bad_download"


def test_corrupt_zip_with_matching_hash(tmp_path):
    rel = make_release(tmp_path / "srv", corrupt_zip=True)
    with pytest.raises(update.UpdateError) as e:
        update.fetch_latest(tmp_path / "dl", "1.1", Log(), fetch=fake_fetch(rel))
    assert e.value.kind == "bad_download"


def test_member_not_matching_inner_manifest(tmp_path):
    rel = make_release(tmp_path / "srv", tamper_member=True)
    with pytest.raises(update.UpdateError) as e:
        update.fetch_latest(tmp_path / "dl", "1.1", Log(), fetch=fake_fetch(rel))
    assert e.value.kind == "bad_download"


def test_manifest_version_must_match_release(tmp_path):
    rel = make_release(tmp_path / "srv", release="1.1", manifest_version="1.0")
    with pytest.raises(update.UpdateError) as e:
        update.fetch_latest(tmp_path / "dl", "1.1", Log(), fetch=fake_fetch(rel))
    assert e.value.kind == "bad_download"


def test_bad_update_json(tmp_path):
    srv = tmp_path / "srv"
    srv.mkdir()
    (srv / "scout-update.json").write_text("{not json", encoding="utf-8")
    with pytest.raises(update.UpdateError) as e:
        update.fetch_latest(tmp_path / "dl", "1.1", Log(), fetch=fake_fetch(srv))
    assert e.value.kind == "bad_info"


def test_newer_exe_available():
    info = update.parse_update(json.dumps({
        "release": "1.2", "commit": "x", "date": "d", "flasher_version": "1.2", "min_flasher": "1.1",
        "firmware_zip": {"name": "scout-firmware.zip", "size": 1, "sha256": "0" * 64}}))
    assert update.newer_exe_available(info, "1.1")
    assert not update.newer_exe_available(info, "1.2")


def test_end_to_end_over_http(tmp_path, monkeypatch):
    rel = make_release(tmp_path / "srv")
    handler = partial(SimpleHTTPRequestHandler, directory=str(rel))
    server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        monkeypatch.setenv("SCOUTFLASHER_UPDATE_URL", "http://127.0.0.1:{}/".format(server.server_port))
        pkg = update.fetch_latest(tmp_path / "dl", "1.1", Log())
        assert pkg.manifest.version == "1.1"
    finally:
        server.shutdown()
        server.server_close()


# ---- hardening: unsafe packages, cleanup, transport errors -------------------

@contextmanager
def serving(root: Path):
    """Serve root over real HTTP on 127.0.0.1; yields the base URL."""
    handler = partial(SimpleHTTPRequestHandler, directory=str(root))
    server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        yield "http://127.0.0.1:{}/".format(server.server_port)
    finally:
        server.shutdown()
        server.server_close()


def members_of(rel: Path) -> dict:
    with zipfile.ZipFile(rel / "scout-firmware.zip") as z:
        return {n: z.read(n) for n in z.namelist()}


def republish(rel: Path, members: dict):
    """Replace the zip with one holding `members`; scout-update.json matches it (size and hash right)."""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        for name, content in members.items():
            zi = zipfile.ZipInfo("placeholder")
            zi.filename = name          # ZipInfo() would rewrite os.sep; keep the name exactly as given
            zi.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(zi, content)
    data = buf.getvalue()
    (rel / "scout-firmware.zip").write_bytes(data)
    info = json.loads((rel / "scout-update.json").read_text(encoding="utf-8"))
    info["firmware_zip"].update(size=len(data), sha256=hashlib.sha256(data).hexdigest())
    (rel / "scout-update.json").write_text(json.dumps(info), encoding="utf-8")


def rewrite_manifest(rel: Path, mutate):
    """Replace manifest.json inside the zip by json.dumps(mutate(parsed manifest))."""
    members = members_of(rel)
    members["manifest.json"] = json.dumps(mutate(json.loads(members["manifest.json"]))).encode("utf-8")
    republish(rel, members)


@pytest.mark.parametrize("evil", ["..\\evil.txt", "../evil.txt", "sub/evil.txt", "C:evil.txt"])
def test_unsafe_member_name_is_refused(tmp_path, evil):
    # A complete, valid package plus one unsafe member, and the hash in scout-update.json matches.
    rel = make_release(tmp_path / "srv")
    republish(rel, {**members_of(rel), evil: b"pwned"})
    dest = tmp_path / "dl"
    with pytest.raises(update.UpdateError) as e:
        update.fetch_latest(dest, "1.1", Log(), fetch=fake_fetch(rel))
    assert e.value.kind == "bad_download"
    assert not (dest / "1.1").exists()
    assert not (dest / "evil.txt").exists() and not (tmp_path / "evil.txt").exists()


@pytest.mark.parametrize("kwargs", [{"tamper_member": True}, {"manifest_version": "1.0"}],
                         ids=["tampered-member", "manifest-version"])
def test_failed_package_is_removed(tmp_path, kwargs):
    rel = make_release(tmp_path / "srv", **kwargs)
    dest = tmp_path / "dl"
    with pytest.raises(update.UpdateError) as e:
        update.fetch_latest(dest, "1.1", Log(), fetch=fake_fetch(rel))
    assert e.value.kind == "bad_download"
    assert not (dest / "1.1").exists()


def test_bad_refetch_leaves_no_package_behind(tmp_path):
    # A good package from an earlier run is replaced; if the new one is bad, nothing stays behind.
    dest = tmp_path / "dl"
    update.fetch_latest(dest, "1.1", Log(), fetch=fake_fetch(make_release(tmp_path / "good")))
    assert (dest / "1.1").is_dir()
    with pytest.raises(update.UpdateError):
        update.fetch_latest(dest, "1.1", Log(), fetch=fake_fetch(make_release(tmp_path / "bad", tamper_member=True)))
    assert not (dest / "1.1").exists()


def test_manifest_without_checksum_for_v3_image(tmp_path):
    # verify() only checks files that have a checksum, so a missing entry must be refused explicitly.
    rel = make_release(tmp_path / "srv")
    members = members_of(rel)
    members["firmware_uvk5_v3.bin"] = b"EVIL"
    republish(rel, members)
    rewrite_manifest(rel, lambda m: {**m, "sha256": {k: v for k, v in m["sha256"].items()
                                                     if k != "firmware_uvk5_v3.bin"}})
    dest = tmp_path / "dl"
    with pytest.raises(update.UpdateError) as e:
        update.fetch_latest(dest, "1.1", Log(), fetch=fake_fetch(rel))
    assert e.value.kind == "bad_download"
    assert "firmware_uvk5_v3.bin" in str(e.value)
    assert not (dest / "1.1").exists()


@pytest.mark.parametrize("mutate", [
    lambda m: {**m, "v1_file": 5},
    lambda m: {**m, "sha256": ["firmware_uvk5_v3.bin"]},
    lambda m: {**m, "sha256": list(m["sha256"])},          # names all there, but a list: verify() needs a dict
    lambda m: {**m, "sha256": {**m["sha256"], "firmware_uvk5_v3.bin": 7}},
    lambda m: {**m, "version": 1.1},
    lambda m: {**m, "unexpected": True},
    lambda m: [1, 2, 3],
], ids=["v1_file-int", "sha256-list", "sha256-list-of-names", "sha256-value-int", "version-float", "extra-key", "not-an-object"])
def test_type_confused_manifest_is_bad_download(tmp_path, mutate):
    rel = make_release(tmp_path / "srv")
    rewrite_manifest(rel, mutate)
    dest = tmp_path / "dl"
    with pytest.raises(update.UpdateError) as e:
        update.fetch_latest(dest, "1.1", Log(), fetch=fake_fetch(rel))
    assert e.value.kind == "bad_download"
    assert not (dest / "1.1").exists()


def test_manifest_naming_a_file_outside_the_package_is_refused(tmp_path):
    # The file exists next to the package folder with the right hash, so verify() alone would accept it.
    rel = make_release(tmp_path / "srv")
    dest = tmp_path / "dl"
    dest.mkdir()
    (dest / "x.bin").write_bytes(b"X")
    rewrite_manifest(rel, lambda m: {**m, "v1_file": "../x.bin",
                                     "sha256": {**m["sha256"], "../x.bin": hashlib.sha256(b"X").hexdigest()}})
    with pytest.raises(update.UpdateError) as e:
        update.fetch_latest(dest, "1.1", Log(), fetch=fake_fetch(rel))
    assert e.value.kind == "bad_download"
    assert not (dest / "1.1").exists()


def test_size_mismatch_with_matching_hash(tmp_path):
    rel = make_release(tmp_path / "srv")
    info = json.loads((rel / "scout-update.json").read_text(encoding="utf-8"))
    info["firmware_zip"]["size"] += 1                       # hash stays right
    (rel / "scout-update.json").write_text(json.dumps(info), encoding="utf-8")
    with pytest.raises(update.UpdateError) as e:
        update.fetch_latest(tmp_path / "dl", "1.1", Log(), fetch=fake_fetch(rel))
    assert e.value.kind == "bad_download"
    assert "expected" in e.value.message                   # the size branch, not the hash branch
    assert not (tmp_path / "dl" / "1.1").exists()


def test_http_404_on_update_json_is_offline(tmp_path, monkeypatch):
    empty = tmp_path / "srv"
    empty.mkdir()
    with serving(empty) as url:
        monkeypatch.setenv("SCOUTFLASHER_UPDATE_URL", url)
        with pytest.raises(update.UpdateError) as e:
            update.fetch_latest(tmp_path / "dl", "1.1", Log())
    assert e.value.kind == "offline"
    assert "404" in str(e.value)


def test_http_404_on_zip_is_offline(tmp_path, monkeypatch):
    rel = make_release(tmp_path / "srv")
    (rel / "scout-firmware.zip").unlink()
    with serving(rel) as url:
        monkeypatch.setenv("SCOUTFLASHER_UPDATE_URL", url)
        with pytest.raises(update.UpdateError) as e:
            update.fetch_latest(tmp_path / "dl", "1.1", Log())
    assert e.value.kind == "offline"
    assert "404" in str(e.value)


def test_url_without_scheme_is_offline(tmp_path, monkeypatch):
    monkeypatch.setenv("SCOUTFLASHER_UPDATE_URL", "myserver/path/")
    log = Log()
    with pytest.raises(update.UpdateError) as e:
        update.fetch_latest(tmp_path / "dl", "1.1", log)
    assert e.value.kind == "offline"
    assert any("Update failed (offline)" in line for line in log)


# ---- HTTPS trust --------------------------------------------------------------

def test_ssl_context_checks_certificates_and_host_names():
    ctx = update._ssl_context()
    assert isinstance(ctx, ssl.SSLContext)
    assert ctx.check_hostname is True
    assert ctx.verify_mode == ssl.CERT_REQUIRED


def test_ssl_context_is_not_strict():
    # Python 3.13's default context sets VERIFY_X509_STRICT; the roots of some antivirus
    # programs that scan HTTPS traffic fail it. Only that one flag goes.
    ctx = update._ssl_context()
    assert not ctx.verify_flags & ssl.VERIFY_X509_STRICT
    kept = ssl.create_default_context().verify_flags & ~ssl.VERIFY_X509_STRICT
    assert ctx.verify_flags == kept


def test_ssl_context_adds_the_certifi_bundle(monkeypatch):
    pytest.importorskip("certifi")
    cafiles = []
    real = ssl.SSLContext.load_verify_locations

    def spy(self, cafile=None, capath=None, cadata=None):
        cafiles.append(cafile)
        return real(self, cafile, capath, cadata)

    monkeypatch.setattr(ssl.SSLContext, "load_verify_locations", spy)
    ctx = update._ssl_context()
    assert update.certifi.where() in cafiles
    assert ctx.check_hostname is True and ctx.verify_mode == ssl.CERT_REQUIRED


@pytest.mark.parametrize("certifi_stub", [
    None,                                                                   # certifi not installed
    types.SimpleNamespace(where=lambda: "no-such-folder/no-such-cacert.pem"),   # bundle not there
], ids=["no-certifi", "missing-bundle"])
def test_ssl_context_survives_a_missing_certifi(monkeypatch, certifi_stub):
    monkeypatch.setattr(update, "certifi", certifi_stub)
    ctx = update._ssl_context()          # the Windows store alone is still a working context
    assert ctx.check_hostname is True and ctx.verify_mode == ssl.CERT_REQUIRED
    assert not ctx.verify_flags & ssl.VERIFY_X509_STRICT


def test_http_get_passes_the_ssl_context_to_urlopen(monkeypatch):
    seen = {}

    class Response:
        def __enter__(self):
            return self

        def __exit__(self, *exc):
            return False

        def read(self, n):
            seen["read"] = n
            return b"payload"

    def fake_urlopen(req, timeout=None, context=None):
        seen.update(url=req.full_url, timeout=timeout, context=context)
        return Response()

    monkeypatch.setattr(update.urllib.request, "urlopen", fake_urlopen)
    assert update._http_get("https://example.invalid/scout-update.json", 7) == b"payload"
    ctx = seen["context"]
    assert isinstance(ctx, ssl.SSLContext)
    assert ctx.check_hostname is True and ctx.verify_mode == ssl.CERT_REQUIRED
    assert not ctx.verify_flags & ssl.VERIFY_X509_STRICT
    assert (seen["url"], seen["timeout"], seen["read"]) == \
        ("https://example.invalid/scout-update.json", 7, update.MAX_BYTES)
