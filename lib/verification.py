"""Isolated, metadata-driven verification-case builds."""

from __future__ import annotations

from contextlib import contextmanager
from dataclasses import dataclass
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import tempfile
from typing import Iterable, Iterator

from .catalog import VerificationCase
from .workspace import WorkspaceError, materialize


ARTIFACT_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]*\.ko\Z")
WARNING_RE = re.compile(r"(?:^|\s)warning:", re.IGNORECASE | re.MULTILINE)


class VerificationError(RuntimeError):
    """A case could not be built or did not satisfy its declared result."""


@dataclass(frozen=True)
class VerificationOutcome:
    case: VerificationCase
    build_root: Path
    artifact: Path | None
    output: str


def _real_directory(path: Path, label: str) -> Path:
    path = Path(path)
    if path.is_symlink():
        raise VerificationError(f"{label} must not be a symlink")
    try:
        resolved = path.resolve(strict=True)
    except OSError as exc:
        raise VerificationError(f"cannot resolve {label}: {path}") from exc
    if not resolved.is_dir():
        raise VerificationError(f"{label} must be a directory: {path}")
    return resolved


def _build_path(case_root: Path, relative: str) -> Path:
    if relative == ".":
        return case_root.resolve(strict=True)
    candidate = PurePosixPath(relative)
    if (
        candidate.is_absolute()
        or not candidate.parts
        or any(part in {"", ".."} for part in candidate.parts)
    ):
        raise VerificationError(f"unsafe build path: {relative!r}")
    build_root = case_root.joinpath(*candidate.parts)
    try:
        resolved = build_root.resolve(strict=True)
    except OSError as exc:
        raise VerificationError(f"build directory does not exist: {relative}") from exc
    if case_root not in resolved.parents and resolved != case_root:
        raise VerificationError(f"build path escapes case tree: {relative}")
    if build_root.is_symlink() or not resolved.is_dir():
        raise VerificationError(f"build path must be a real directory: {relative}")
    return resolved


def _artifact(path: Path, case: VerificationCase) -> Path:
    try:
        mode = path.lstat().st_mode
    except FileNotFoundError as exc:
        raise VerificationError(
            f"verification case {case.id!r} produced no artifact: {path.name}"
        ) from exc
    if stat.S_ISLNK(mode) or not stat.S_ISREG(mode):
        raise VerificationError(
            f"verification case {case.id!r} artifact is not a regular file: {path.name}"
        )
    return path


@contextmanager
def build_verification_cases(
    repository_root: Path,
    source_tree: Path,
    declared_files: Iterable[str],
    build_relative: str,
    artifact_basename: str,
    cases: Iterable[VerificationCase],
    timeout_seconds: int,
) -> Iterator[tuple[VerificationOutcome, ...]]:
    """Build every case in isolation and retain its tree for the context lifetime."""
    repository_root = _real_directory(repository_root, "repository root")
    source_tree = _real_directory(source_tree, "source tree")
    if not ARTIFACT_RE.fullmatch(artifact_basename):
        raise VerificationError("artifact must be a portable .ko basename")
    if isinstance(timeout_seconds, bool) or not isinstance(timeout_seconds, int) or timeout_seconds <= 0:
        raise VerificationError("timeout must be a positive integer")
    case_values = tuple(cases)
    if not case_values:
        raise VerificationError("verification cases must be non-empty")
    declared_values = tuple(declared_files)
    try:
        temporary_context = tempfile.TemporaryDirectory(prefix="rootkitlings-cases-")
    except OSError as exc:
        raise VerificationError(f"cannot create verification directory: {exc}") from exc
    with temporary_context as temporary:
        case_parent = Path(temporary)
        outcomes = []
        for case in case_values:
            try:
                case_root = materialize(
                    source_tree, case_parent, case.id, declared_values
                )
            except WorkspaceError as exc:
                raise VerificationError(
                    f"cannot materialize verification case {case.id!r}: {exc}"
                ) from exc
            build_root = _build_path(case_root, build_relative)
            artifact = build_root / artifact_basename
            if artifact.exists() or artifact.is_symlink():
                raise VerificationError(
                    f"verification case {case.id!r} starts with an artifact: "
                    f"{artifact_basename}"
                )
            command = [
                "make", "-C", os.fspath(repository_root),
                f"MODULE_DIR={build_root}", "W=1",
                *(f"{name}={value}" for name, value in case.build_variables),
                "module",
            ]
            try:
                result = subprocess.run(
                    command,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    timeout=timeout_seconds,
                    check=False,
                )
            except subprocess.TimeoutExpired as exc:
                output = exc.stdout or ""
                if isinstance(output, bytes):
                    output = output.decode(errors="replace")
                raise VerificationError(
                    f"verification case {case.id!r} timed out after "
                    f"{timeout_seconds}s\n{output}"
                ) from exc
            except OSError as exc:
                raise VerificationError(
                    f"cannot execute verification case {case.id!r}: {exc}"
                ) from exc
            output = result.stdout
            if case.build_output_contains and case.build_output_contains not in output:
                raise VerificationError(
                    f"verification case {case.id!r} output lacks required marker "
                    f"{case.build_output_contains!r}\n{output}"
                )
            if case.expect_build == "fail":
                if result.returncode == 0:
                    raise VerificationError(
                        f"verification case {case.id!r} unexpectedly built successfully"
                    )
                outcomes.append(VerificationOutcome(case, build_root, None, output))
                continue
            if case.expect_build != "pass":
                raise VerificationError(
                    f"verification case {case.id!r} has invalid expected result"
                )
            if result.returncode != 0:
                raise VerificationError(
                    f"verification case {case.id!r} unexpectedly failed to build\n{output}"
                )
            if WARNING_RE.search(output):
                raise VerificationError(
                    f"verification case {case.id!r} built with warnings\n{output}"
                )
            outcomes.append(
                VerificationOutcome(case, build_root, _artifact(artifact, case), output)
            )
        yield tuple(outcomes)
