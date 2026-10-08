"""Versioned, deterministic learner state with atomic locking and writes."""

from __future__ import annotations

from contextlib import contextmanager
from dataclasses import dataclass, field
import fcntl
import os
from pathlib import Path
import re
import tempfile
import tomllib
from typing import Iterator


SCHEMA_VERSION = 2
ID_RE = re.compile(r"[a-z0-9][a-z0-9-]*\Z")


class StateError(ValueError):
    """Learner state is malformed or incompatible with the active course."""


@dataclass
class ExerciseState:
    baseline_hash: str = ""
    workspace_hash: str = ""
    passed_snapshot: str = ""
    attempts: int = 0
    solution_revealed: bool = False


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

    def validate(self, valid_ids: set[str], route: tuple[str, ...], revision: str) -> None:
        _validate_contract(valid_ids, route, revision)
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
        required_completed = tuple(item for item in route if item in self.completed)
        if required_completed != route[:len(required_completed)]:
            raise StateError("completed required-route IDs must form an exact prefix")
        for stable_id, item in self.exercises.items():
            if item.attempts < 0:
                raise StateError(f"negative attempt count for {stable_id}")
            for value in (item.baseline_hash, item.workspace_hash, item.passed_snapshot):
                if value and not re.fullmatch(r"[0-9a-f]{64}", value):
                    raise StateError(f"invalid content hash for {stable_id}: {value!r}")


def new_state(route: tuple[str, ...], valid_ids: set[str], revision: str) -> LearnerState:
    _validate_contract(valid_ids, route, revision)
    return LearnerState(revision, route)


def _validate_contract(valid_ids: set[str], route: tuple[str, ...], revision: str) -> None:
    if not revision or "\n" in revision or "\r" in revision:
        raise StateError("curriculum revision must be a non-empty single-line string")
    if not route or len(route) != len(set(route)):
        raise StateError("required route must be non-empty and contain unique IDs")
    if any(not ID_RE.fullmatch(item) for item in valid_ids | set(route)):
        raise StateError("stable IDs must be lowercase slug tokens")
    if not set(route) <= valid_ids:
        raise StateError(f"route contains unknown IDs: {sorted(set(route) - valid_ids)}")


def load_state(path: Path, valid_ids: set[str], route: tuple[str, ...], revision: str) -> LearnerState:
    """Load and validate state, returning new empty state when the file is absent."""
    path = Path(path)
    if not path.exists():
        return new_state(route, valid_ids, revision)
    try:
        data = tomllib.loads(path.read_text(encoding="utf-8"))
    except (OSError, tomllib.TOMLDecodeError) as exc:
        raise StateError(f"cannot read learner state: {exc}") from exc
    if set(data) != {"schema_version", "curriculum_revision", "route", "completed", "exercises"}:
        raise StateError("state has missing or unknown top-level fields")
    if data["schema_version"] != SCHEMA_VERSION or isinstance(data["schema_version"], bool):
        raise StateError(f"state schema_version must be {SCHEMA_VERSION}")
    if not isinstance(data["curriculum_revision"], str):
        raise StateError("curriculum_revision must be a string")
    saved_route = _string_array(data["route"], "route")
    completed = set(_string_array(data["completed"], "completed"))
    raw_exercises = data["exercises"]
    if not isinstance(raw_exercises, dict):
        raise StateError("exercises must be a table")
    exercises: dict[str, ExerciseState] = {}
    fields = {"baseline_hash", "workspace_hash", "passed_snapshot", "attempts", "solution_revealed"}
    for stable_id, raw in raw_exercises.items():
        if not isinstance(raw, dict) or set(raw) != fields:
            raise StateError(f"exercise state fields mismatch for {stable_id}")
        if any(not isinstance(raw[key], str) for key in ("baseline_hash", "workspace_hash", "passed_snapshot")):
            raise StateError(f"exercise hashes must be strings for {stable_id}")
        attempts = raw["attempts"]
        revealed = raw["solution_revealed"]
        if isinstance(attempts, bool) or not isinstance(attempts, int):
            raise StateError(f"attempts must be an integer for {stable_id}")
        if not isinstance(revealed, bool):
            raise StateError(f"solution_revealed must be boolean for {stable_id}")
        exercises[stable_id] = ExerciseState(
            raw["baseline_hash"], raw["workspace_hash"], raw["passed_snapshot"], attempts, revealed
        )
    state = LearnerState(data["curriculum_revision"], saved_route, completed, exercises)
    state.validate(valid_ids, route, revision)
    return state


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
            f"workspace_hash = {_quote(item.workspace_hash)}",
            f"passed_snapshot = {_quote(item.passed_snapshot)}",
            f"attempts = {item.attempts}",
            f"solution_revealed = {'true' if item.solution_revealed else 'false'}",
        ]
    return "\n".join(lines) + "\n"


def _quote(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    return f'"{escaped}"'


def _array(*groups: object) -> str:
    return "[" + ", ".join(_quote(value) for values in groups for value in values) + "]"


def _atomic_write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
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

    def __init__(self, path: Path, valid_ids: set[str], route: tuple[str, ...], revision: str):
        self.path = Path(path)
        self.valid_ids = set(valid_ids)
        self.route = tuple(route)
        self.revision = revision
        _validate_contract(self.valid_ids, self.route, self.revision)

    @contextmanager
    def locked(self) -> Iterator[LearnerState]:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        lock_path = self.path.with_name(self.path.name + ".lock")
        with lock_path.open("a+b") as lock:
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
            state = load_state(self.path, self.valid_ids, self.route, self.revision)
            try:
                yield state
            except Exception:
                raise
            else:
                state.validate(self.valid_ids, self.route, self.revision)
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
                yield load_state(self.path, self.valid_ids, self.route, self.revision)
            finally:
                fcntl.flock(lock.fileno(), fcntl.LOCK_UN)
