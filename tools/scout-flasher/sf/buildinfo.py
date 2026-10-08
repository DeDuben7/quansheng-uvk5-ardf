"""Which build this is: commit + date, written by tools/release.ps1 into the
generated (gitignored) sf/_build_info.py. Source runs show "dev"."""

from sf import FLASHER_VERSION

try:
    from sf import _build_info as _bi   # generated at release time
    COMMIT = _bi.COMMIT
    BUILD_DATE = _bi.BUILD_DATE
except ImportError:
    COMMIT = "dev"
    BUILD_DATE = ""


def label() -> str:
    return "ScoutFlasher {} · {}".format(FLASHER_VERSION, COMMIT)
