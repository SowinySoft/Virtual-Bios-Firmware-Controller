#!/usr/bin/env python3
"""Run the breadboard validation workflow and capture the output to a log file."""

from __future__ import annotations

import argparse
import subprocess
import sys
from datetime import datetime
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the breadboard validation workflow and log it")
    parser.add_argument("--image", default=None, help="Optional image file to stage")
    parser.add_argument("--offset", default="0x2000", help="Extension-flash offset")
    parser.add_argument("--port", default=None, help="Optional serial port")
    parser.add_argument("--log-dir", default="../../logs", help="Directory for timestamped run logs")
    args = parser.parse_args()

    log_dir = Path(args.log_dir).resolve()
    log_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    log_path = log_dir / f"breadboard-{stamp}.log"

    cmd = [sys.executable, "-m", "vbfc_cli.breadboard_runner"]
    if args.image:
        cmd.extend(["--image", args.image])
    if args.offset:
        cmd.extend(["--offset", args.offset])
    if args.port:
        cmd.extend(["--port", args.port])

    print(f"Logging to {log_path}")
    with log_path.open("w", encoding="utf-8") as handle:
        proc = subprocess.run(cmd, cwd=Path(__file__).resolve().parent.parent, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        handle.write(proc.stdout)
        print(proc.stdout, end="")
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
