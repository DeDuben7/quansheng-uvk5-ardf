import sys
import threading
from pathlib import Path

from sf.flash import flash_v1, parse_k5tool_progress

ACK = """[TRACE] recv Packet2FlashWriteAck {{
  HdrSize=8
  SequenceId=0x1d00eccd
  ChunkNumber=0x{:04x}
  Result=0
}}
"""
REQ = """[TRACE] send Packet2FlashWriteReq {{
  HdrSize=268
  SequenceId=0x1d00eccd
  ChunkNumber=0x{:04x}
  ChunkCount=0x0090
  Size=0x100
}}
"""


def k5tool_log(acked: int) -> str:
    return "".join(REQ.format(i) + ACK.format(i) for i in range(acked))


def test_progress_none_before_first_chunk():
    assert parse_k5tool_progress("[TRACE] Opening COM9\n") is None


def test_progress_counts_acked_chunks():
    assert parse_k5tool_progress(k5tool_log(5)) == (5, 144)
    assert parse_k5tool_progress(k5tool_log(144)) == (144, 144)


def test_progress_ignores_unacked_request():
    text = k5tool_log(3) + REQ.format(3)
    assert parse_k5tool_progress(text) == (3, 144)


FAKE_K5TOOL = r"""
import sys, time
args = sys.argv[1:]
assert args[0] == '-port' and args[2] == '-wrflash', args
log = open('K5TOOL.log', 'w')
for i in range(4):
    log.write('''[TRACE] send Packet2FlashWriteReq {{
  ChunkNumber=0x{0:04x}
  ChunkCount=0x0004
}}
[TRACE] recv Packet2FlashWriteAck {{
  ChunkNumber=0x{0:04x}
  Result=0
}}
'''.format(i))
    log.flush()
    time.sleep(0.05)
log.close()
print('Opening ' + args[1])
if MODE == 'ok':
    print('Done')
    sys.exit(0)
print('[ERROR] something went wrong')
sys.exit(-2)
"""


def run_fake(tmp_path: Path, mode: str):
    script = tmp_path / "fake_k5tool.py"
    script.write_text(FAKE_K5TOOL.replace("MODE", repr(mode)), encoding="utf-8")
    image = tmp_path / "fw.packed.bin"
    image.write_bytes(b"\x00" * 64)
    seen, lines = [], []
    ok = flash_v1("COM9", image, progress=lambda d, t: seen.append((d, t)),
                  log=lines.append, cancel=threading.Event(),
                  k5tool_cmd=[sys.executable, str(script)])
    return ok, seen, lines


def test_flash_v1_success(tmp_path):
    ok, seen, lines = run_fake(tmp_path, "ok")
    assert ok
    assert seen[-1] == (4, 4)
    assert any("Done" in line for line in lines)
    assert any("ChunkNumber=0x0003" in line for line in lines)   # K5TOOL.log copied into our log


def test_flash_v1_failure_exit_code(tmp_path):
    ok, _, lines = run_fake(tmp_path, "fail")
    assert not ok
    assert any("ERROR" in line for line in lines)
