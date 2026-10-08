"""Offline tests for the ProtocolV2 packet framing (no radio needed).

    python -m unittest test_msg      (from tools/k5v3-flasher/)
"""

import unittest

import msg as mm


def _ack_packet(page_index: int) -> bytes:
    m = mm.Msg.make(mm.MSG_PROG_FW_RESP, 8)
    m.set_word_LE(4, 0x12345678)
    m.set_hw_LE(8, page_index)
    m.set_hw_LE(10, 0)
    return bytes(mm.make_packet(m.buf))


class FetchTest(unittest.TestCase):

    def test_whole_packet(self):
        buf = bytearray(_ack_packet(3))
        msg = mm.fetch(buf)
        self.assertIsNotNone(msg)
        self.assertEqual(mm.MSG_PROG_FW_RESP, msg.get_msg_type())
        self.assertEqual(3, msg.get_hw_LE(8))
        self.assertEqual(0, len(buf))

    def test_packet_split_across_reads(self):
        # A serial read can return a packet in pieces. fetch() must wait for
        # the rest instead of discarding the partial packet.
        pkt = _ack_packet(4)
        for cut in range(1, len(pkt)):
            buf = bytearray(pkt[:cut])
            self.assertIsNone(mm.fetch(buf), f"cut={cut}")
            buf.extend(pkt[cut:])
            msg = mm.fetch(buf)
            self.assertIsNotNone(msg, f"packet lost when split at byte {cut}")
            self.assertEqual(4, msg.get_hw_LE(8))

    def test_resync_after_garbage(self):
        buf = bytearray(b"\x00\x11\xab\x22" + _ack_packet(7))
        msg = None
        for _ in range(4):
            msg = mm.fetch(buf) or msg
        self.assertIsNotNone(msg)
        self.assertEqual(7, msg.get_hw_LE(8))


class _FakeBootloader:
    """Minimal pyserial stand-in for the ProtocolV2 bootloader.

    Emits a device-info beacon every 200 ms (also during programming, to
    exercise interleaving), ACKs every PROG_FW page, and hands RX bytes back
    in small random chunks the way a USB-serial driver does.
    """

    def __init__(self, seed=1):
        import random
        import time
        self.clock = time.monotonic
        self.rnd = random.Random(seed)
        self.rx = bytearray()      # device -> host, not yet read
        self.tx = bytearray()      # host -> device, not yet parsed
        self.pages = {}
        self.page_cnt = None
        self.unlocked = False
        self.next_beacon = self.clock()

    def _send(self, m):
        self.rx.extend(mm.make_packet(m.buf))

    def _beacon(self):
        m = mm.Msg.make(mm.MSG_NOTIFY_DEV_INFO, 32)
        m.buf[20:27] = b"7.00.07"
        self._send(m)

    def readinto(self, buf):
        if self.clock() >= self.next_beacon:
            self.next_beacon += 0.2
            self._beacon()
        n = min(len(buf), len(self.rx), self.rnd.randint(0, 7))
        buf[:n] = self.rx[:n]
        del self.rx[:n]
        return n

    def write(self, data):
        self.tx.extend(data)
        while True:
            m = mm.fetch(self.tx)
            if not m:
                break
            t = m.get_msg_type()
            if t == mm.MSG_NOTIFY_BL_VER:
                self.unlocked = bytes(m.buf[4:8]) == b"7.00"
            elif t == mm.MSG_PROG_FW and self.unlocked:
                idx, cnt = m.get_hw_LE(8), m.get_hw_LE(10)
                self.page_cnt = cnt
                self.pages[idx] = bytes(m.buf[16:16 + 256])
                r = mm.Msg.make(mm.MSG_PROG_FW_RESP, 8)
                r.set_word_LE(4, m.get_word_LE(4))
                r.set_hw_LE(8, idx)
                self._send(r)

    def flush(self):
        pass


class ProgrammerTest(unittest.TestCase):

    def test_full_flash_no_resends(self):
        import io, contextlib
        import _prog as pp

        image = bytes(i * 7 & 0xFF for i in range(14 * 1024 + 100))
        dev = _FakeBootloader()
        prog = pp.Programmer(dev, image, "7.00")
        out = io.StringIO()
        deadline = dev.clock() + 30
        with contextlib.redirect_stdout(out):
            while dev.clock() < deadline and prog.loop():
                pass
        log = out.getvalue()
        self.assertTrue(prog.ok, log[-500:])
        self.assertIn(", 0 resends", log)
        self.assertEqual(57, dev.page_cnt)
        flashed = b"".join(dev.pages[i] for i in range(dev.page_cnt))
        self.assertEqual(image, flashed[:len(image)])


if __name__ == "__main__":
    unittest.main()
