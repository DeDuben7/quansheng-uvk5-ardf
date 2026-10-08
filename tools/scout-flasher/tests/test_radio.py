import msg as mm

from sf.radio import Beacon, classify, parse_beacons

# The V1 bootloader's device-info beacon exactly as it came off the wire
# (K5TOOL.log, 2026-09-28, the developer's V1 on COM9).
V1_BEACON_RAW = bytes.fromhex(
    "abcd24000e6934e62f930944396682755d4416946d6c1ce61cbf3d700f05e340"
    "2709e980166c14c6ffffdcba")


def v3_beacon(version: bytes = b"7.00.07") -> bytes:
    m = mm.Msg.make(mm.MSG_NOTIFY_DEV_INFO, 32)
    m.buf[20:20 + len(version)] = version
    return bytes(mm.make_packet(m.buf))


def test_classify():
    assert classify("2.00.06") == "V1"
    assert classify("2.01.23") == "V1"
    assert classify("7.00.07") == "V3"
    assert classify("5.00.01") is None
    assert classify("") is None


def test_real_v1_beacon():
    assert parse_beacons(bytearray(V1_BEACON_RAW)) == [Beacon("2.00.06", "V1")]


def test_v3_beacon():
    assert parse_beacons(bytearray(v3_beacon())) == [Beacon("7.00.07", "V3")]


def test_unknown_bootloader():
    assert parse_beacons(bytearray(v3_beacon(b"5.00.01"))) == [Beacon("5.00.01", None)]


def test_beacon_split_across_reads():
    buf = bytearray(V1_BEACON_RAW[:17])
    assert parse_beacons(buf) == []
    buf.extend(V1_BEACON_RAW[17:])
    assert parse_beacons(buf) == [Beacon("2.00.06", "V1")]


def test_partial_packet_containing_start_marker_does_not_hang():
    # A packet still arriving whose payload happens to contain AB CD.
    pkt = bytearray(v3_beacon())
    pkt[12:14] = b"\xab\xcd"
    buf = bytearray(pkt[:30])
    assert parse_beacons(buf) == []


def test_noise_before_beacon():
    buf = bytearray(b"\x00\x13garbage" + V1_BEACON_RAW + v3_beacon())
    assert [b.kind for b in parse_beacons(buf)] == ["V1", "V3"]
