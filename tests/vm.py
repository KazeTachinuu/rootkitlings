"""Thin wrapper around virtme-ng for kernel module integration tests."""

from __future__ import annotations

import os
import subprocess
from pathlib import Path


REPO = Path(__file__).resolve().parent.parent
KDIR = Path(f"/lib/modules/{os.uname().release}/build")


class VMError(RuntimeError):
    pass


class VMResult:
    def __init__(self, completed: subprocess.CompletedProcess[str]) -> None:
        self.returncode = completed.returncode
        self.output = (completed.stdout or "") + (completed.stderr or "")

    @property
    def ok(self) -> bool:
        return self.returncode == 0


class VM:
    """Boot the host kernel in a disposable VM via virtme-ng."""

    def __init__(self) -> None:
        self._tools = REPO / ".rklings/build/tools"
        self._lib = REPO / "vm/checks/lib.sh"
        self._ui = REPO / "scripts/lib/ui.sh"

    def run_check(self, check: str | Path, ko: str | Path,
                  timeout: int = 60,
                  check_args: tuple[str, ...] = ()) -> VMResult:
        ko = Path(ko)
        args_str = " ".join(f"'{a}'" for a in check_args)
        cmd = [
            "vng", "--run", "--rw", "--user", "root", "--exec",
            f"export RKLINGS_LIB={self._lib}"
            f" RKLINGS_UI={self._ui}"
            f" RKLINGS_DIR={ko.parent}"
            f" PATH={self._tools}:$PATH"
            f" && {check} {args_str}",
        ]
        try:
            result = subprocess.run(
                cmd, capture_output=True, text=True,
                timeout=timeout, cwd=str(REPO),
            )
        except subprocess.TimeoutExpired as err:
            raise VMError(f"VM timed out after {timeout}s") from err
        return VMResult(result)


def build_module(src: Path, variables: dict[str, str] | None = None) -> Path:
    """Build a kernel module against the host kernel."""
    var_args = [f"{k}={v}" for k, v in (variables or {}).items()]
    subprocess.run(
        ["make", "-s", "-C", str(KDIR), f"M={src}", "modules", *var_args],
        check=True, capture_output=True, text=True,
    )
    matches = list(src.glob("*.ko"))
    if not matches:
        raise VMError(f"no .ko produced in {src}")
    return matches[0]


def clean_module(src: Path) -> None:
    """Remove build artifacts from a source directory."""
    subprocess.run(
        ["make", "-s", "-C", str(KDIR), f"M={src}", "clean"],
        capture_output=True, text=True,
    )


def build_tool(name: str) -> Path:
    out = REPO / ".rklings/build/tools"
    target = out / name
    subprocess.run(
        ["make", "-s", "-C", str(REPO), f"TOOL_OUT={out}", str(target)],
        check=True, capture_output=True, text=True,
    )
    return target
