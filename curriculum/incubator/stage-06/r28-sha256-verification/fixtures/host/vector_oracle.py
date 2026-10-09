#!/usr/bin/env python3
"""Check the frozen guest vectors with an implementation outside the lesson."""

from __future__ import annotations

import hashlib
import pathlib
import re
import sys


def fail(message: str) -> "NoReturn":
    raise SystemExit(f"[x] {message}")


def main() -> None:
    if len(sys.argv) != 2:
        fail(f"usage: {pathlib.Path(sys.argv[0]).name} SELFTEST_C")

    source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
    table = source.split("static int rk_expect", 1)[0]
    octets = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", table))
    if len(octets) != 7 * hashlib.sha256().digest_size:
        fail("could not read the seven frozen SHA-256 vectors")

    numbered = bytes(range(65))
    messages = (b"", b"abc", numbered[:55], numbered[:56], numbered[:63],
                numbered[:64], numbered[:65])
    for index, message in enumerate(messages):
        expected = octets[index * 32:(index + 1) * 32]
        if hashlib.sha256(message).digest() != expected:
            fail(f"frozen SHA-256 vector {index} is incorrect")

    print("[+] frozen SHA-256 vectors match the independent hashlib oracle")


if __name__ == "__main__":
    main()
