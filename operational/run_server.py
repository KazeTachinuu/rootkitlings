#!/usr/bin/env python3
"""Standalone operator server entry point."""

import sys
from pathlib import Path

SERVER_DIR = Path(__file__).resolve().parent / "server"
sys.path.insert(0, str(SERVER_DIR))

from control_console import OperatorError
from server import run_server

ROOT = Path(__file__).resolve().parent


def main():
    try:
        status = run_server(
            ROOT,
            transcript=lambda parts: None,
            output=lambda s: sys.stdout.write(s),
            success=lambda s: sys.stdout.write(f"[+] {s}\n"),
            warning=lambda s: sys.stderr.write(f"[!] {s}\n"),
        )
    except OperatorError as error:
        sys.exit(f"[x] {error}")
    sys.exit(status)


if __name__ == "__main__":
    main()
