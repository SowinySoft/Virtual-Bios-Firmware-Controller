#!/usr/bin/env python3
"""Helpers for the breadboard validation workflow.

This script prints an example command sequence for the core validation steps:
1. scan the controller,
2. upload a small image,
3. add a shadow-map entry,
4. switch to shadow mode,
5. verify the redirected read path.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def build_sequence(image: str | None = None, offset: str = "0x2000") -> list[str]:
    commands = []
    commands.append("vbfc-cli scan")
    if image:
        img = Path(image).resolve()
        commands.append(f"vbfc-cli flash restore {img} {offset}")
        commands.append(f"vbfc-cli map add --start 0xFF0000 --size 64K --source ext --ext-offset {offset}")
        commands.append("vbfc-cli mode shadow")
    else:
        commands.append("vbfc-cli mode pass-through")
    return commands


def main() -> int:
    parser = argparse.ArgumentParser(description="Print a breadboard validation command sequence")
    parser.add_argument("--image", default=None, help="Optional image file to stage before shadow validation")
    parser.add_argument("--offset", default="0x2000", help="Extension-flash offset to use for staged image")
    args = parser.parse_args()

    for cmd in build_sequence(args.image, args.offset):
        print(cmd)
    return 0


if __name__ == "__main__":
    sys.exit(main())
