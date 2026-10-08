from sf.manifest import load_manifest, verify, write_manifest


def test_round_trip_and_verify(tmp_path):
    (tmp_path / "v1.packed.bin").write_bytes(b"v1")
    (tmp_path / "v3.bin").write_bytes(b"v3")
    (tmp_path / "manual.pdf").write_bytes(b"%PDF")
    written = write_manifest(tmp_path, "1.0", "v1.packed.bin", "v3.bin", "manual.pdf")
    loaded = load_manifest(tmp_path)
    assert loaded == written
    assert loaded.version == "1.0"
    assert verify(tmp_path, loaded) == []

    (tmp_path / "v3.bin").write_bytes(b"tampered")
    (tmp_path / "manual.pdf").unlink()
    assert sorted(verify(tmp_path, loaded)) == ["manual.pdf", "v3.bin"]
