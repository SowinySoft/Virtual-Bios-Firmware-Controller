#!/usr/bin/env python3
"""Run the breadboard validation sequence against a connected VBFC device."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def run(cmd: str) -> None:
    print(f"$ {cmd}")
    completed = subprocess.run(cmd, shell=True, check=False)
    if completed.returncode != 0:
        print(f"[runner] command exited with code {completed.returncode}")


def _cli_cmd(port: str | None) -> str:
    return "py -m vbfc_cli.main" if port is None else f"py -m vbfc_cli.main --port {port}"


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the breadboard validation workflow")
    parser.add_argument("--image", default=None, help="Optional image file to stage")
    parser.add_argument("--offset", default="0x2000", help="Extension-flash offset")
    parser.add_argument("--port", default=None, help="Optional serial port")
    args = parser.parse_args()

    if args.image is not None and not Path(args.image).exists():
        print(f"[runner] image not found: {args.image}")
        return 2

    cli_cmd = _cli_cmd(args.port)
    run(f"{cli_cmd} scan")
    if args.image:
        image_path = Path(args.image).resolve()
        run(f"{cli_cmd} flash restore {image_path} {args.offset}")
        run(f"{cli_cmd} map add --start 0xFF0000 --size 64K --source ext --ext-offset {args.offset}")
        run(f"{cli_cmd} mode shadow")
    else:
        run(f"{cli_cmd} mode pass-through")
    return 0


if __name__ == "__main__":
    sys.exit(main())
