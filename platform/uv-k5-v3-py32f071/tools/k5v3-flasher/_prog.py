# Copyright (c) 2025 muzkr
#
#   https://github.com/muzkr
#
# Licensed under the MIT License (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at the root of this repository.
#
#     Unless required by applicable law or agreed to in writing, software
#     distributed under the License is distributed on an "AS IS" BASIS,
#     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#     See the License for the specific language governing permissions and
#     limitations under the License.
#


from serial import Serial
import msg as mm
from datetime import datetime
import math


_QUIT = "quit"

# scout addition (not in upstream muzkr serialtool), mirroring UVTools2's
# programFirmware(): wait up to 3 s for a page's PROG_FW_RESP, then resend it,
# giving up after 3 consecutive resends. Upstream waits forever. With the
# msg.fetch() split-packet fix, a resend is a real fault, not routine.
# Units are 1/100 s.
_PAGE_TIMEOUT_TICKS = 300   # 3 s per page before resend (UVTools2: 300 x 10 ms)
_PAGE_MAX_RETRIES = 3       # UVTools2 MAX_RETRIES

# UVTools2 waits 200 ms after the handshake and then drops any buffered
# device-info beacons before sending the first page.
_POST_HANDSHAKE_TICKS = 20


class Programmer:

    def __init__(self, ser: Serial, fw_image: bytes, bl_ver: str, verbose=False):
        self._ser = ser
        self._fw_image = fw_image
        self.bl_ver = bl_ver
        self.verbose = verbose
        self.ok = False         # set once every page has been ACKed
        self._t0 = _timestamp()
        self._state = _Init(self)
        # self._state = _Logging(self)

    def trace(self, direction: str, raw: bytes, msg: mm.Msg):
        """--verbose: one line per packet, raw (on-the-wire) bytes in hex."""
        if not self.verbose:
            return
        dt = (_timestamp() - self._t0) / 100
        print(
            "[{:7.2f}] {} type=0x{:04x} len={:3} |{}".format(
                dt, direction, msg.get_msg_type(), len(raw), bytes(raw).hex(" ")
            )
        )

    def loop(self) -> bool:
        next = self._state.loop()
        if isinstance(next, str) and next == _QUIT:
            return False

        if next:
            self._state = next

        return True


class _MsgReceiver:

    def __init__(self, ser: Serial):
        self.ser = ser
        self.rx_buf = bytearray(256)
        self.msg_buf = bytearray()

    def recv_msg(self) -> mm.Msg | None:
        self._rx()
        return mm.fetch(self.msg_buf)

    def peek_packet(self) -> bytes:
        """Raw bytes of the packet fetch() would return next (for --verbose)."""
        buf = self.msg_buf
        begin = buf.find(b"\xab\xcd")
        if -1 == begin or len(buf) - begin < 4:
            return b""
        end = begin + 8 + mm._get_hw_LE(buf, begin + 2)
        return bytes(buf[begin:end]) if len(buf) >= end else b""

    def _rx(self) -> int:

        len1 = 0
        buf = self.rx_buf
        while True:
            len2 = self.ser.readinto(buf)
            if len2 > 0:
                self.msg_buf.extend(memoryview(buf)[:len2])
                len1 += len2
            if len2 < len(buf):
                break

        return len1


class _State:
    def __init__(self, prog: Programmer):
        self.prog = prog
        self.msg_receiver = _MsgReceiver(prog._ser)

    def loop(self) -> str | object | None:
        raise NotImplementedError()

    def recv_msg(self) -> mm.Msg | None:
        rcv = self.msg_receiver
        if not self.prog.verbose:
            return rcv.recv_msg()
        rcv._rx()
        raw = rcv.peek_packet()
        msg = mm.fetch(rcv.msg_buf)
        if msg:
            self.prog.trace("RX", raw, msg)
        return msg

    def send_msg(self, msg: mm.Msg):
        pack = mm.make_packet(msg.buf)
        self.prog.trace("TX", pack, msg)
        ser = self.prog._ser
        ser.write(pack)
        ser.flush()


class _Init(_State):

    def __init__(self, prog: Programmer):
        super().__init__(prog)

        self.last_ts = 0  # Timestamp of last message, in 1/100 sec
        self.acc = 0

    def loop(self) -> _State | None:

        msg = self.recv_msg()
        if not msg:
            return None

        ts = _timestamp()
        dt = ts - self.last_ts
        self.last_ts = ts

        # print(f"[{ts}] ", end="")
        # _print_msg(msg)

        if mm.MSG_NOTIFY_DEV_INFO != msg.get_msg_type():
            self.acc = 0
            return None

        # Normally message interval is 20
        MIN_INTERVAL = 5
        MAX_INTERVAL = 100

        if dt < MIN_INTERVAL or dt > MAX_INTERVAL:
            print(".", end="")
            self.acc = 0
            return None

        if 0 == self.acc:
            print()
            print("Establishing contact to device..")
            _Init.print_dev_info(msg)

        self.acc += 1
        if self.acc < 5:
            return None

        print("Device detected")

        bl_ver = _Init.get_bl_ver(msg)
        bl_ver2 = self.prog.bl_ver
        # scout: --bl-ver is at most 4 chars ("7.00") while the device
        # reports the full version ("7.00.07"), so compare as a prefix.
        if "*" != bl_ver2 and not bl_ver.startswith(bl_ver2):
            print(
                "!!! WARNING: BL version does not match! Expecting {}, actually {}".format(
                    bl_ver2, bl_ver
                )
            )

        return _Handshake(self.prog)

    def get_bl_ver(msg: mm.Msg) -> str:

        buf = msg.buf
        end = buf.find(b"\x00", 20, 36)
        if -1 == end:
            end = 36

        s = buf[20:end].decode("ascii")
        return s

    def print_dev_info(msg: mm.Msg):

        print("UID: ", end="")

        with memoryview(msg.buf) as view:
            uid = view[4:20]
            for b in uid:
                print(f" {b:02x}", end="")
        print()

        # BL versio -----------

        s = _Init.get_bl_ver(msg)
        print("BL version:", s)


class _Handshake(_State):

    def __init__(self, prog):
        super().__init__(prog)

        bl_ver = prog.bl_ver
        if len(bl_ver) > 4:
            bl_ver = bl_ver[:4]
        self.bl_ver = bl_ver

        self.acc = 0

    def loop(self) -> _State | None:
        msg = self.recv_msg()
        if not msg:
            return None

        # _print_msg(msg)

        if mm.MSG_NOTIFY_DEV_INFO != msg.get_msg_type():
            self.acc = 0
            return None

        if 0 == self.acc:
            print("Handshaking..")

        msg2 = self.make_msg()
        self.send_msg(msg2)

        self.acc += 1
        if self.acc < 3:
            return None

        print("Handshake done")
        return _Settle(self.prog)

    def make_msg(self):
        msg: mm.Msg = mm.Msg.make(mm.MSG_NOTIFY_BL_VER, 4)

        with memoryview(msg.buf) as view:
            len1 = len(self.bl_ver)
            view[4 : 4 + len1] = self.bl_ver.encode("ascii")[:len1]

        return msg


class _Settle(_State):
    """Pause after the handshake, discarding beacons still in flight."""

    def __init__(self, prog):
        super().__init__(prog)
        self.start_ts = _timestamp()

    def loop(self) -> _State | None:
        self.recv_msg()
        if (_timestamp() - self.start_ts) < _POST_HANDSHAKE_TICKS:
            return None
        return _ProgFw(self.prog)


class _ProgFw(_State):

    def __init__(self, prog):
        super().__init__(prog)

        img = prog._fw_image
        img_len = len(img)
        page_cnt = math.ceil(img_len / 256)

        self.image = img
        self.x4 = 0xFFFFFFFF & _timestamp()
        self.page_index = 0
        self.page_cnt = page_cnt
        self.expect_resp = False
        self.sent_ts = 0      # when the current page was (re)sent
        self.retries = 0      # resend count for the current page
        self.total_resends = 0
        self.start_ts = _timestamp()

    def loop(self) -> _State | None:

        if not self.expect_resp:

            if self.retries == 0:
                print(
                    "Programming page {} / {}..".format(
                        self.page_index + 1, self.page_cnt
                    )
                )
            else:
                print(
                    "  page {} resend (retry {}/{})".format(
                        self.page_index + 1, self.retries, _PAGE_MAX_RETRIES
                    )
                )

            msg = self.make_msg(self.page_index)
            self.send_msg(msg)

            self.sent_ts = _timestamp()
            self.expect_resp = True
            return None

        # ------------
        #  Receive response

        msg = self.recv_msg()
        if not msg:
            # scout addition: resend the page if no ACK within the timeout,
            # instead of waiting forever (bounded, as in UVTools2).
            if (_timestamp() - self.sent_ts) > _PAGE_TIMEOUT_TICKS:
                print("  page {} no ACK after {:.1f} s".format(
                    self.page_index + 1, _PAGE_TIMEOUT_TICKS / 100))
                self.retries += 1
                self.total_resends += 1
                if self.retries > _PAGE_MAX_RETRIES:
                    print(
                        "Programming failed: page {} got no ACK after {} retries".format(
                            self.page_index + 1, _PAGE_MAX_RETRIES
                        )
                    )
                    return _QUIT
                self.expect_resp = False  # triggers a resend next loop
            return None

        if mm.MSG_PROG_FW_RESP != msg.get_msg_type():
            return None

        assert 8 == msg.get_data_len()

        x4 = msg.get_word_LE(4)
        page_index = msg.get_hw_LE(8)
        err = msg.get_hw_LE(10)

        # scout addition: ignore a stale/duplicate ACK for a page we've already
        # advanced past (can arrive after a resend), so it can't be mistaken for
        # the current page's ACK and skip a page.
        if page_index != self.page_index:
            print("  ACK for page {} while waiting for page {} (ignored)".format(
                page_index + 1, self.page_index + 1))
            return None

        if 0 != err:
            print(
                "Programming failed: err = {}, page index = {}".format(err, page_index)
            )
            # Retry this page (bounded by the same retry cap).
            self.retries += 1
            self.total_resends += 1
            if self.retries > _PAGE_MAX_RETRIES:
                print(
                    "Programming failed: page {} err after {} retries".format(
                        self.page_index + 1, _PAGE_MAX_RETRIES
                    )
                )
                return _QUIT
            self.expect_resp = False
            return None

        self.page_index += 1
        self.expect_resp = False
        self.retries = 0  # reset for the next page

        if self.page_index < self.page_cnt:
            return None

        print("Firmware program done: {} pages in {:.1f} s, {} resends".format(
            self.page_cnt, (_timestamp() - self.start_ts) / 100,
            self.total_resends))
        self.prog.ok = True
        # return _Logging(self.prog)
        return _QUIT

    def make_msg(self, page_index: int):

        msg: mm.Msg = mm.Msg.make(mm.MSG_PROG_FW, 268)
        msg.set_word_LE(4, self.x4)
        msg.set_hw_LE(8, page_index)
        msg.set_hw_LE(10, self.page_cnt)

        image_off = page_index * 256
        len1 = len(self.image) - image_off

        if len1 > 256:
            len1 = 256

        if len1 > 0:
            # msg.set_data(self.image, page_index * 256, len1)
            with memoryview(msg.buf) as view:
                view[16 : 16 + len1] = memoryview(self.image)[
                    image_off : image_off + len1
                ]

        return msg


def _timestamp() -> int:
    return int(datetime.now().timestamp() * 100)


def _timestamp_str() -> str:

    now = datetime.now().time()
    return "{:02}:{:02}:{:02}.{:02}".format(
        now.hour,
        now.minute,
        now.second,
        int(now.microsecond / 10000) % 100,
    )


def _print_msg_raw(msg: mm.Msg):
    for b in msg.buf:
        print(f" {b:02x}", end="")
    print()
