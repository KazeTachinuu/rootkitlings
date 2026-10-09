"""Controlled environment for reproducible course-module builds."""

from __future__ import annotations

import os


def module_build_environment() -> dict[str, str]:
    environment = {
        "PATH": os.environ.get("PATH", os.defpath),
        "LC_ALL": "C",
        "LANG": "C",
        "TZ": "UTC",
        "ARCH": "x86_64",
        "CC": "gcc",
        "HOSTCC": "gcc",
    }
    if "TMPDIR" in os.environ:
        environment["TMPDIR"] = os.environ["TMPDIR"]
    return environment
