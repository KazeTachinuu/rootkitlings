"""VM integration test helpers using the course kernel."""

from __future__ import annotations

import subprocess
from pathlib import Path


REPO = Path(__file__).resolve().parent.parent


def _course_kernel(subcommand: str) -> Path:
    result = subprocess.run(
        [REPO / "scripts/kernel", subcommand],
        capture_output=True, text=True, check=True,
    )
    return Path(result.stdout.strip())


KDIR = _course_kernel("build-dir")


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
    """Run a guest check via vm/boot with the course kernel."""

    def run_check(self, check: str | Path, ko: str | Path,
                  timeout: int = 60,
                  check_args: tuple[str, ...] = ()) -> VMResult:
        cmd = [
            str(REPO / "vm/boot"),
            "--check", str(check),
        ]
        for arg in check_args:
            cmd += ["--check-arg", arg]
        cmd.append(str(ko))
        try:
            result = subprocess.run(
                cmd, capture_output=True, text=True,
                timeout=timeout, cwd=str(REPO),
            )
        except subprocess.TimeoutExpired as err:
            raise VMError(f"VM timed out after {timeout}s") from err
        return VMResult(result)


def build_module(src: Path, variables: dict[str, str] | None = None) -> Path:
    """Build a kernel module against the course kernel."""
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
