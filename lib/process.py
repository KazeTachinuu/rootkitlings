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

    def merged_output(first: str, second: str) -> str:
        return second if second.startswith(first) else first + second

    def timeout_output(error: subprocess.TimeoutExpired) -> str:
        output = error.output or ""
        if isinstance(output, bytes):
            return output.decode(errors="replace")
        return output

    def stop_group() -> str:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            remaining, _ = process.communicate(timeout=1)
            return remaining or ""
        except subprocess.TimeoutExpired as term_error:
            partial = timeout_output(term_error)
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                remaining, _ = process.communicate(timeout=1)
                return merged_output(partial, remaining or "")
            except subprocess.TimeoutExpired as kill_error:
                partial = merged_output(partial, timeout_output(kill_error))
                # A descendant can deliberately leave the process group while
                # retaining the captured pipe. Do not let that escaped pipe
                # turn timeout cleanup into an unbounded wait.
                if process.stdout is not None:
                    process.stdout.close()
                try:
                    process.wait(timeout=1)
                except subprocess.TimeoutExpired as wait_error:
                    raise ProcessError(
                        f"command did not stop after SIGKILL: {' '.join(values)}"
                    ) from wait_error
                return partial

    try:
        output, _ = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired as error:
        partial = timeout_output(error)
        stopped_output = stop_group()
        output = merged_output(partial, stopped_output)
        raise ProcessTimeout(values, timeout or 0, output) from error
    except BaseException:
        stop_group()
        raise
    return subprocess.CompletedProcess(values, process.returncode, output, None)
