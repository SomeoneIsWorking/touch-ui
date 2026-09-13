#!/usr/bin/env python3
"""Render the controls over a game-like frame and save a PNG to look at.

The controls are drawn onto a real SDL renderer, so what this writes is what
the port paints: use it to check sizes and reachability on a phone-shaped
surface without a phone.

    python3 tools/capture.py scratch/touch-controls.png --width 2400 --height 1080
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HARNESS = ROOT / "tests" / "capture_touch_ui.cpp"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--width", type=int, default=2400)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--scale", type=float, default=3.0)
    args = parser.parse_args()

    build = ROOT / "build"
    if not (build / "CMakeCache.txt").is_file():
        raise SystemExit("capture: run tools/verify.py once to configure the build")
    binary = build / "touch_ui_capture"
    subprocess.run(
        [
            "cmake",
            "--build",
            str(build),
            "--target",
            "touch_ui_capture",
        ],
        cwd=ROOT,
        check=True,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            str(binary),
            str(args.output.resolve()),
            str(args.width),
            str(args.height),
            str(args.scale),
        ],
        cwd=ROOT,
        check=True,
    )
    print(f"capture: wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
