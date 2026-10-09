"""Versioned, deterministic learner state with atomic locking and writes."""

from __future__ import annotations

from contextlib import contextmanager
from dataclasses import dataclass, field
import fcntl
import os
from pathlib import Path
import re
import stat
import tempfile
import tomllib
from typing import Iterator, Mapping


SCHEMA_VERSION = 4
ID_RE = re.compile(r"[a-z0-9][a-z0-9-]*\Z")


class StateError(ValueError):
    """Learner state is malformed or incompatible with the active course."""


@dataclass
class ExerciseState:
    baseline_hash: str = ""
    workspace_hash: str = ""
    passed_snapshot: str = ""
    attempts: int = 0
    baseline_provider: str = ""


@dataclass
class LearnerState:
    curriculum_revision: str
    route: tuple[str, ...]
    completed: set[str] = field(default_factory=set)
    exercises: dict[str, ExerciseState] = field(default_factory=dict)

    @property
    def current(self) -> str | None:
        """Return the first incomplete required-route ID, derived rather than stored."""
        return next((item for item in self.route if item not in self.completed), None)

    def exercise(self, stable_id: str) -> ExerciseState:
        if not ID_RE.fullmatch(stable_id):
            raise StateError(f"invalid stable ID: {stable_id!r}")
        return self.exercises.setdefault(stable_id, ExerciseState())

    def clear_progress(self, stable_ids: set[str]) -> None:
        """Forget completion and per-exercise state for an explicit ID set."""
        invalid = {item for item in stable_ids if not ID_RE.fullmatch(item)}
        if invalid:
            raise StateError(f"invalid stable IDs: {sorted(invalid)}")
        self.completed.difference_update(stable_ids)
        for stable_id in stable_ids:
            self.exercises.pop(stable_id, None)

    def validate(
        self,
        valid_ids: set[str],
        route: tuple[str, ...],
        revision: str,
        prerequisites: Mapping[str, frozenset[str]],
        baseline_providers: Mapping[str, str | None],
    ) -> None:
        _validate_contract(
            valid_ids, route, revision, prerequisites, baseline_providers
        )
        if self.curriculum_revision != revision:
            raise StateError(
                f"curriculum revision mismatch: state={self.curriculum_revision!r}, active={revision!r}"
            )
        if self.route != route:
            raise StateError("saved required route differs from the active route for this revision")
        unknown = (self.completed | set(self.exercises)) - valid_ids
        if unknown:
            raise StateError(f"state contains unknown stable IDs: {sorted(unknown)}")
        if not self.completed <= set(route):
            elective_completed = self.completed - set(route)
            if not elective_completed <= valid_ids:
                raise StateError(f"state contains unknown stable IDs: {sorted(elective_completed - valid_ids)}")
        for stable_id in self.completed:
            missing = set(prerequisites[stable_id]) - self.completed
            if missing:
                raise StateError(
                    f"completion for {stable_id!r} lacks prerequisites: {sorted(missing)}"
                )
        for stable_id, item in self.exercises.items():
            if item.attempts < 0:
                raise StateError(f"negative attempt count for {stable_id}")
            for value in (item.baseline_hash, item.workspace_hash, item.passed_snapshot):
                if value and not re.fullmatch(r"[0-9a-f]{64}", value):
                    raise StateError(f"invalid content hash for {stable_id}: {value!r}")
            expected_provider = baseline_providers[stable_id] or ""
            if item.baseline_hash:
                if item.baseline_provider != expected_provider:
                    raise StateError(
                        f"baseline provider mismatch for {stable_id}: "
                        f"state={item.baseline_provider!r}, expected={expected_provider!r}"
                    )
                if expected_provider and expected_provider not in self.completed:
                    raise StateError(
                        f"baseline provider for {stable_id} is not completion-available: "
                        f"{expected_provider}"
                    )
            elif item.baseline_provider:
                raise StateError(
                    f"baseline provider without baseline hash for {stable_id}"
                )


def new_state(
    route: tuple[str, ...],
    valid_ids: set[str],
    revision: str,
    prerequisites: Mapping[str, frozenset[str]],
    baseline_providers: Mapping[str, str | None],
) -> LearnerState:
    _validate_contract(valid_ids, route, revision, prerequisites, baseline_providers)
    return LearnerState(revision, route)


def _validate_contract(
    valid_ids: set[str],
    route: tuple[str, ...],
    revision: str,
    prerequisites: Mapping[str, frozenset[str]],
    baseline_providers: Mapping[str, str | None],
) -> None:
    if not revision or "\n" in revision or "\r" in revision:
        raise StateError("curriculum revision must be a non-empty single-line string")
    if not route or len(route) != len(set(route)):
        raise StateError("required route must be non-empty and contain unique IDs")
    if any(not ID_RE.fullmatch(item) for item in valid_ids | set(route)):
        raise StateError("stable IDs must be lowercase slug tokens")
    if not set(route) <= valid_ids:
        raise StateError(f"route contains unknown IDs: {sorted(set(route) - valid_ids)}")
    if set(prerequisites) != valid_ids:
        raise StateError("prerequisite map must cover every stable ID exactly")
    if set(baseline_providers) != valid_ids:
        raise StateError("baseline provider map must cover every stable ID exactly")
    for stable_id, provider in baseline_providers.items():
        if provider is None:
            continue
        if provider not in valid_ids or provider == stable_id:
            raise StateError(f"invalid baseline provider for {stable_id}: {provider!r}")
        if provider not in prerequisites[stable_id]:
            raise StateError(
                f"baseline provider for {stable_id} must be a completion dependency"
            )
    positions = {stable_id: index for index, stable_id in enumerate(route)}
    visiting: set[str] = set()
    visited: set[str] = set()

    def visit(stable_id: str) -> None:
        if stable_id in visiting:
            raise StateError(f"prerequisite cycle at {stable_id}")
        if stable_id in visited:
            return
        visiting.add(stable_id)
        dependencies = prerequisites[stable_id]
        unknown = set(dependencies) - valid_ids
        if unknown:
            raise StateError(f"unknown prerequisites for {stable_id}: {sorted(unknown)}")
        if stable_id in dependencies:
            raise StateError(f"self prerequisite at {stable_id}")
        if stable_id in positions:
            for dependency in dependencies:
                if dependency not in positions or positions[dependency] >= positions[stable_id]:
                    raise StateError(
                        f"required prerequisite {dependency!r} is not earlier than {stable_id!r}"
                    )
        for dependency in dependencies:
            visit(dependency)
        visiting.remove(stable_id)
        visited.add(stable_id)

    for stable_id in valid_ids:
        visit(stable_id)


def load_state(
    path: Path,
    valid_ids: set[str],
    route: tuple[str, ...],
    revision: str,
    prerequisites: Mapping[str, frozenset[str]],
    baseline_providers: Mapping[str, str | None],
) -> LearnerState:
    """Load and validate state, returning new empty state when the file is absent."""
    path = Path(path)
    if path.parent.is_symlink():
        raise StateError(f"learner state parent must not be a symlink: {path.parent}")
    if path.is_symlink():
        raise StateError(f"learner state must not be a symlink: {path}")
    if not path.exists():
        return new_state(
            route, valid_ids, revision, prerequisites, baseline_providers
        )
    if not path.is_file():
        raise StateError(f"learner state must be a regular file: {path}")
    try:
        state = parse_state(_read_regular_bytes(path).decode("utf-8"))
    except (OSError, UnicodeDecodeError, tomllib.TOMLDecodeError) as exc:
        raise StateError(f"cannot read learner state: {exc}") from exc
    state.validate(
        valid_ids, route, revision, prerequisites, baseline_providers
    )
    return state


def parse_state(content: str) -> LearnerState:
    """Parse and structurally validate state without binding it to a catalog."""
    try:
        data = tomllib.loads(content)
    except tomllib.TOMLDecodeError as exc:
        raise StateError(f"cannot parse learner state: {exc}") from exc
    if set(data) != {"schema_version", "curriculum_revision", "route", "completed", "exercises"}:
        raise StateError("state has missing or unknown top-level fields")
    if data["schema_version"] != SCHEMA_VERSION or isinstance(data["schema_version"], bool):
        raise StateError(f"state schema_version must be {SCHEMA_VERSION}")
    if not isinstance(data["curriculum_revision"], str):
        raise StateError("curriculum_revision must be a string")
    saved_route = _string_array(data["route"], "route")
    if not saved_route:
        raise StateError("saved route must be non-empty")
    completed = set(_string_array(data["completed"], "completed"))
    raw_exercises = data["exercises"]
    if not isinstance(raw_exercises, dict):
        raise StateError("exercises must be a table")
    exercises: dict[str, ExerciseState] = {}
    fields = {
        "baseline_hash", "baseline_provider", "workspace_hash",
        "passed_snapshot", "attempts",
    }
    for stable_id, raw in raw_exercises.items():
        if not isinstance(raw, dict) or set(raw) != fields:
            raise StateError(f"exercise state fields mismatch for {stable_id}")
        if any(not isinstance(raw[key], str) for key in (
            "baseline_hash", "baseline_provider", "workspace_hash", "passed_snapshot"
        )):
            raise StateError(f"exercise hashes must be strings for {stable_id}")
        attempts = raw["attempts"]
        if isinstance(attempts, bool) or not isinstance(attempts, int):
            raise StateError(f"attempts must be an integer for {stable_id}")
        exercises[stable_id] = ExerciseState(
            baseline_hash=raw["baseline_hash"],
            baseline_provider=raw["baseline_provider"],
            workspace_hash=raw["workspace_hash"],
            passed_snapshot=raw["passed_snapshot"],
            attempts=attempts,
        )
    state = LearnerState(data["curriculum_revision"], saved_route, completed, exercises)
    _validate_structural_state(state)
    return state


def _validate_structural_state(state: LearnerState) -> None:
    if not state.curriculum_revision or any(
        value in state.curriculum_revision for value in ("\n", "\r")
    ):
        raise StateError("curriculum revision must be a non-empty single-line string")
    identifiers = set(state.route) | state.completed | set(state.exercises)
    if any(not ID_RE.fullmatch(item) for item in identifiers):
        raise StateError("state contains invalid stable IDs")
    if not state.route or len(state.route) != len(set(state.route)):
        raise StateError("saved route must be non-empty and contain unique IDs")
    for stable_id, item in state.exercises.items():
        if item.attempts < 0:
            raise StateError(f"negative attempt count for {stable_id}")
        for value in (item.baseline_hash, item.workspace_hash, item.passed_snapshot):
            if value and not re.fullmatch(r"[0-9a-f]{64}", value):
                raise StateError(f"invalid content hash for {stable_id}: {value!r}")
        if item.baseline_provider and not ID_RE.fullmatch(item.baseline_provider):
            raise StateError(
                f"invalid baseline provider for {stable_id}: {item.baseline_provider!r}"
            )


def _string_array(value: object, name: str) -> tuple[str, ...]:
    if not isinstance(value, list) or any(not isinstance(item, str) for item in value):
        raise StateError(f"{name} must be an array of strings")
    if len(value) != len(set(value)):
        raise StateError(f"{name} must not contain duplicates")
    return tuple(value)


def dump_state(state: LearnerState) -> str:
    """Serialize state in stable order without volatile timestamps."""
    lines = [
        f"schema_version = {SCHEMA_VERSION}",
        f"curriculum_revision = {_quote(state.curriculum_revision)}",
        f"route = {_array(state.route)}",
        f"completed = {_array((item for item in state.route if item in state.completed), (item for item in sorted(state.completed) if item not in state.route))}",
        "",
        "[exercises]",
    ]
    for stable_id in sorted(state.exercises):
        item = state.exercises[stable_id]
        lines += [
            "",
            f"[exercises.{_quote(stable_id)}]",
            f"baseline_hash = {_quote(item.baseline_hash)}",
            f"baseline_provider = {_quote(item.baseline_provider)}",
            f"workspace_hash = {_quote(item.workspace_hash)}",
            f"passed_snapshot = {_quote(item.passed_snapshot)}",
            f"attempts = {item.attempts}",
        ]
    return "\n".join(lines) + "\n"


def _quote(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    return f'"{escaped}"'


def _array(*groups: object) -> str:
    return "[" + ", ".join(_quote(value) for values in groups for value in values) + "]"


def _atomic_write(path: Path, content: str) -> None:
    _atomic_write_bytes(path, content.encode("utf-8"))


def _read_regular_bytes(path: Path) -> bytes:
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(path, flags)
    except OSError as exc:
        raise StateError(f"cannot safely open learner state: {path}") from exc
    with os.fdopen(descriptor, "rb") as stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise StateError(f"learner state must be a regular file: {path}")
        return stream.read()


def _atomic_write_bytes(path: Path, content: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink():
        raise StateError(f"refusing to replace symlink: {path}")
    fd, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        directory_fd = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    finally:
        if temporary.exists():
            temporary.unlink()


class StateStore:
    """Coordinate state access through a sibling advisory lock file."""

    def __init__(
        self,
        path: Path,
        valid_ids: set[str],
        route: tuple[str, ...],
        revision: str,
        prerequisites: Mapping[str, frozenset[str]],
        baseline_providers: Mapping[str, str | None],
    ):
        self.path = Path(path)
        self.valid_ids = set(valid_ids)
        self.route = tuple(route)
        self.revision = revision
        self.prerequisites = {
            stable_id: frozenset(values)
            for stable_id, values in prerequisites.items()
        }
        self.baseline_providers = dict(baseline_providers)
        _validate_contract(
            self.valid_ids,
            self.route,
            self.revision,
            self.prerequisites,
            self.baseline_providers,
        )

    @contextmanager
    def locked(self) -> Iterator[LearnerState]:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        lock_path = self.path.with_name(self.path.name + ".lock")
        with lock_path.open("a+b") as lock:
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
            state = load_state(
                self.path,
                self.valid_ids,
                self.route,
                self.revision,
                self.prerequisites,
                self.baseline_providers,
            )
            try:
                yield state
            except Exception:
                raise
            else:
                state.validate(
                    self.valid_ids,
                    self.route,
                    self.revision,
                    self.prerequisites,
                    self.baseline_providers,
                )
                _atomic_write(self.path, dump_state(state))
            finally:
                fcntl.flock(lock.fileno(), fcntl.LOCK_UN)

    @contextmanager
    def read_locked(self) -> Iterator[LearnerState]:
        """Read consistently under a shared lock without creating or rewriting state.toml."""
        self.path.parent.mkdir(parents=True, exist_ok=True)
        lock_path = self.path.with_name(self.path.name + ".lock")
        with lock_path.open("a+b") as lock:
            fcntl.flock(lock.fileno(), fcntl.LOCK_SH)
            try:
                yield load_state(
                    self.path,
                    self.valid_ids,
                    self.route,
                    self.revision,
                    self.prerequisites,
                    self.baseline_providers,
                )
            finally:
                fcntl.flock(lock.fileno(), fcntl.LOCK_UN)
