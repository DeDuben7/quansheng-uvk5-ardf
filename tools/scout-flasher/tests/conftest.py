import sys
from pathlib import Path

TOOL_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOL_DIR))

from sf import paths  # noqa: E402

paths.enable_k5v3_imports()
