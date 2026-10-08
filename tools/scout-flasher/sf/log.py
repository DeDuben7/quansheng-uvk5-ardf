"""The run log: one text file per run on the desktop, for the user to send on.

Every line is flushed immediately, so the file is complete even if the
program is closed in the middle of a flash.
"""

import threading
from datetime import datetime
from pathlib import Path


class RunLog:

    def __init__(self, path: Path):
        self.path = path
        self._lock = threading.Lock()
        path.parent.mkdir(parents=True, exist_ok=True)
        self._f = open(path, "a", encoding="utf-8")

    @staticmethod
    def default_name(now: datetime | None = None) -> str:
        now = now or datetime.now()
        return "ScoutFlasher-log-{:%Y%m%d-%H%M%S}.txt".format(now)

    def write(self, line: str) -> None:
        stamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        with self._lock:
            for part in str(line).splitlines() or [""]:
                self._f.write("{}  {}\n".format(stamp, part))
            self._f.flush()

    def section(self, title: str) -> None:
        self.write("")
        self.write("==== {} ====".format(title))

    def close(self) -> None:
        with self._lock:
            self._f.close()
