#!/usr/bin/env python3
"""Root-level launcher for test/demo_serial_count_all.py."""

from pathlib import Path
import runpy


if __name__ == "__main__":
    script = Path(__file__).resolve().parent / "test" / "demo_serial_count_all.py"
    runpy.run_path(str(script), run_name="__main__")
