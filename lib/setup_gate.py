"""Safe, identity-free persistence for the authorized setup gate."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import stat
import tempfile
import tomllib
from typing import Mapping


SCHEMA_VERSION = 1
SETUP_STAMP_PATH = Path(".rootkitlings/setup.toml")
FIELDS = {
    "schema_version",
    "authorized",
    "policy_version",
    "course_revision",
    "kernel_release",
    "boundary_hash",
}


class SetupGateError(ValueError):
    pass


@dataclass(frozen=True)
class SetupStamp:
    policy_version: str
    course_revision: str
    kernel_release: str
    boundary_hash: str
    authorized: bool = True

    def __post_init__(self) -> None:
        _nonempty("policy_version", self.policy_version)
        _nonempty("course_revision", self.course_revision)
        _nonempty("kernel_release", self.kernel_release)
        if self.authorized is not True:
            raise SetupGateError("authorized must be true")
        if (
            not isinstance(self.boundary_hash, str)
            or len(self.boundary_hash) != 64
            or any(character not in "0123456789abcdef" for character in self.boundary_hash)
        ):
            raise SetupGateError("boundary_hash must be a lowercase SHA-256 digest")


@dataclass(frozen=True)
class SetupStatus:
    ready: bool
    reason: str
    stamp: SetupStamp | None = None


def boundary_hash(inputs: Mapping[str, str | bytes]) -> str:
    """Hash the caller's exact named policy/probe inputs with length framing."""
    if not isinstance(inputs, Mapping) or not inputs:
        raise SetupGateError("boundary inputs must be a nonempty mapping")
    digest = hashlib.sha256(b"rootkitlings-setup-boundary-v1\0")
    for name in sorted(inputs):
        if not isinstance(name, str) or not name or any(ord(value) < 32 for value in name):
            raise SetupGateError("boundary input names must be nonempty printable strings")
        value = inputs[name]
        if isinstance(value, str):
            kind = b"text"
            data = value.encode("utf-8")
        elif isinstance(value, bytes):
            kind = b"bytes"
            data = value
        else:
            raise SetupGateError("boundary input values must be strings or bytes")
        encoded_name = name.encode("utf-8")
        for part in (encoded_name, kind, data):
            digest.update(len(part).to_bytes(8, "big"))
            digest.update(part)
    return digest.hexdigest()


def read_setup_stamp(path: Path) -> SetupStamp:
    """Read and validate a setup stamp without following the final symlink."""
    path = Path(path)
    _safe_parent(path)
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(path, flags)
    except FileNotFoundError as exc:
        raise SetupGateError("setup stamp is missing") from exc
    except OSError as exc:
        raise SetupGateError(f"cannot safely open setup stamp: {exc.strerror}") from exc
    try:
        metadata = os.fstat(descriptor)
        if not stat.S_ISREG(metadata.st_mode):
            raise SetupGateError("setup stamp must be a regular file")
        with os.fdopen(descriptor, "rb", closefd=False) as source:
            raw = source.read()
    finally:
        os.close(descriptor)
    try:
        data = tomllib.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, tomllib.TOMLDecodeError) as exc:
        raise SetupGateError(f"invalid setup stamp TOML: {exc}") from exc
    if set(data) != FIELDS:
        raise SetupGateError("setup stamp fields do not match schema v1")
    if type(data["schema_version"]) is not int or data["schema_version"] != SCHEMA_VERSION:
        raise SetupGateError("setup stamp schema_version must be integer 1")
    if data["authorized"] is not True:
        raise SetupGateError("authorized must be true")
    return SetupStamp(
        authorized=data["authorized"],
        policy_version=data["policy_version"],
        course_revision=data["course_revision"],
        kernel_release=data["kernel_release"],
        boundary_hash=data["boundary_hash"],
    )


def setup_status(path: Path, expected: SetupStamp) -> SetupStatus:
    """Report readiness or one precise stale/invalid reason code."""
    try:
        actual = read_setup_stamp(path)
    except SetupGateError as exc:
        message = str(exc)
        reason = "missing" if message == "setup stamp is missing" else f"invalid: {message}"
        return SetupStatus(False, reason)
    comparisons = (
        ("policy-version-changed", actual.policy_version, expected.policy_version),
        ("course-revision-changed", actual.course_revision, expected.course_revision),
        ("kernel-release-changed", actual.kernel_release, expected.kernel_release),
        ("boundary-changed", actual.boundary_hash, expected.boundary_hash),
    )
    for reason, current, wanted in comparisons:
        if current != wanted:
            return SetupStatus(False, reason, actual)
    return SetupStatus(True, "ready", actual)


def write_setup_stamp(
    path: Path,
    stamp: SetupStamp,
    *,
    checks_passed: bool,
) -> None:
    """Atomically write a mode-0600 stamp only after caller checks passed."""
    if checks_passed is not True:
        raise SetupGateError("setup stamp requires successful caller checks")
    path = Path(path)
    parent = _safe_parent(path)
    if path.is_symlink() or (path.exists() and not path.is_file()):
        raise SetupGateError("setup stamp destination must be a regular file or absent")
    text = "\n".join((
        f"schema_version = {SCHEMA_VERSION}",
        "authorized = true",
        f"policy_version = {json.dumps(stamp.policy_version)}",
        f"course_revision = {json.dumps(stamp.course_revision)}",
        f"kernel_release = {json.dumps(stamp.kernel_release)}",
        f"boundary_hash = {json.dumps(stamp.boundary_hash)}",
        "",
    )).encode("utf-8")
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=parent)
    temporary = Path(temporary_name)
    try:
        os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "wb", closefd=False) as target:
            target.write(text)
            target.flush()
            os.fsync(descriptor)
        os.close(descriptor)
        descriptor = -1
        os.replace(temporary, path)
        directory = os.open(parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if descriptor >= 0:
            os.close(descriptor)
        if temporary.exists():
            temporary.unlink()


def _nonempty(name: str, value: object) -> str:
    if not isinstance(value, str) or not value or any(ord(character) < 32 for character in value):
        raise SetupGateError(f"{name} must be a nonempty printable string")
    return value


def _safe_parent(path: Path) -> Path:
    parent = path.parent
    if parent.is_symlink():
        raise SetupGateError("setup stamp parent must not be a symlink")
    try:
        resolved = parent.resolve(strict=True)
    except OSError as exc:
        raise SetupGateError(f"cannot resolve setup stamp parent: {parent}") from exc
    if not resolved.is_dir():
        raise SetupGateError("setup stamp parent must be a directory")
    return resolved
