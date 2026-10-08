"""Developer tool: render scoutflasher.svg into scoutflasher.ico (multi-size,
for the EXE and the window) and scoutflasher.png (256 px, for Tk).

    python tools/scout-flasher/icon/build_icon.py

Uses headless Edge (always present on Windows 10/11) to draw the SVG and
Pillow to crop/scale. Not part of the shipped EXE; the two generated files
are committed, so a normal build never has to run this.
"""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
SVG = HERE / "scoutflasher.svg"
ICO = HERE / "scoutflasher.ico"
PNG = HERE / "scoutflasher.png"

SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
MASTER = 512          # the SVG is drawn at 512 x 512, then scaled down
PNG_SIZE = 256

EDGE_CANDIDATES = [
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
]

WRAPPER = ('<!doctype html><html><body style="margin:0;background:transparent">'
           '<img src="scoutflasher.svg" width="{n}" height="{n}" style="display:block">'
           '</body></html>')


def find_edge() -> Path:
    for p in EDGE_CANDIDATES:
        if os.path.isfile(p):
            return Path(p)
    found = shutil.which("msedge")
    if found:
        return Path(found)
    raise SystemExit("Microsoft Edge not found; it is needed to render the SVG.")


def render_master(edge: Path) -> Image.Image:
    """The SVG as a 512 x 512 RGBA image with a transparent background."""
    with tempfile.TemporaryDirectory(prefix="scoutflasher-icon-", ignore_cleanup_errors=True) as tmp:
        tmp = Path(tmp)
        shutil.copyfile(SVG, tmp / "scoutflasher.svg")
        wrapper = tmp / "wrapper.html"
        wrapper.write_text(WRAPPER.format(n=MASTER), encoding="utf-8")
        shot = tmp / "shot.png"
        cmd = [str(edge), "--headless=new", "--disable-gpu", "--no-first-run",
               "--user-data-dir=" + str(tmp / "profile"), "--hide-scrollbars",
               "--default-background-color=00000000", "--window-size=512,700",
               "--screenshot=" + str(shot), wrapper.as_uri()]
        res = subprocess.run(cmd, timeout=120, capture_output=True, text=True, errors="replace")
        if res.returncode != 0 or not shot.is_file():
            raise SystemExit("Edge failed (exit code {}, screenshot {}).\n{}{}".format(
                res.returncode, "written" if shot.is_file() else "missing", res.stdout, res.stderr))
        with Image.open(shot) as im:
            return im.convert("RGBA").crop((0, 0, MASTER, MASTER))


def main() -> int:
    master = render_master(find_edge())
    master.resize((PNG_SIZE, PNG_SIZE), Image.LANCZOS).save(PNG)
    master.save(ICO, sizes=[(s, s) for s in SIZES])
    print("wrote {} and {}".format(ICO, PNG))
    return 0


if __name__ == "__main__":
    sys.exit(main())
