"""Bounded argv-only subprocess execution with process-group cleanup."""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import signal
import subprocess
from typing import Sequence


class ProcessError(RuntimeError):
    """A subprocess could not be started or did not stop cleanly."""


@dataclass(frozen=True)
class ProcessTimeout(ProcessError):
    argv: tuple[str, ...]
    timeout: int | float
    output: str

    def __str__(self) -> str:
        return f"command timed out after {self.timeout}s: {' '.join(self.argv)}\n{self.output}"


def run_process(
    argv: Sequence[str | os.PathLike[str]],
    *,
    cwd: Path | str | None = None,
    timeout: int | float | None = None,
    capture: bool = False,
    discard_stdin: bool = False,
) -> subprocess.CompletedProcess[str]:
    """Run an argv command in its own process group and kill the group on timeout."""
    values = tuple(os.fspath(value) for value in argv)
    if not values or any(not value for value in values):
        raise ProcessError("command argv must contain non-empty values")
    try:
        process = subprocess.Popen(
            values,
            cwd=cwd,
            stdin=subprocess.DEVNULL if discard_stdin else None,
            stdout=subprocess.PIPE if capture else None,
            stderr=subprocess.STDOUT if capture else None,
            text=True,
            start_new_session=True,
        )
    except OSError as error:
        raise ProcessError(f"cannot execute {' '.join(values)}: {error}") from error
    def stop_group() -> str:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            remaining, _ = process.communicate(timeout=1)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            remaining, _ = process.communicate()
        return remaining or ""

    try:
        output, _ = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired as error:
        partial = error.output or ""
        if isinstance(partial, bytes):
            partial = partial.decode(errors="replace")
        stopped_output = stop_group()
        output = stopped_output if stopped_output.startswith(partial) else partial + stopped_output
        raise ProcessTimeout(values, timeout or 0, output) from error
    except BaseException:
        stop_group()
        raise
    return subprocess.CompletedProcess(values, process.returncode, output, None)
