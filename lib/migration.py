"""Explicit, deterministic learner-state migrations.

File migration is restricted to private, user-owned, non-symlinked directories.
Workspace relocation remains the caller's responsibility.
"""

from __future__ import annotations

from dataclasses import dataclass
import fcntl
import hashlib
import os
from pathlib import Path
import stat
import tempfile
from typing import Callable, Mapping

from lib.state import ID_RE, ExerciseState, LearnerState, StateError, dump_state, parse_state


class MigrationError(ValueError):
    pass


@dataclass(frozen=True)
class CurriculumContract:
    revision: str
    ids: frozenset[str]
    route: tuple[str, ...]
    prerequisites: Mapping[str, frozenset[str]]
    baseline_providers: Mapping[str, str | None] | None = None


@dataclass(frozen=True)
class CompletionCredit:
    """Completion evidence only; it never selects history."""

    destination: str
    all_of: tuple[str, ...]


@dataclass(frozen=True)
class HistoryMerge:
    """History sources only; it never grants completion."""

    destination: str
    sources: tuple[str, ...]


@dataclass(frozen=True)
class SnapshotCompatibility:
    source: str
    destination: str


@dataclass(frozen=True)
class MigrationPlan:
    source_revision: str
    destination_revision: str
    renames: Mapping[str, str]
    removals: frozenset[str]
    completion_credits: tuple[CompletionCredit, ...] = ()
    history_merges: tuple[HistoryMerge, ...] = ()
    compatible_snapshots: tuple[SnapshotCompatibility, ...] = ()


SnapshotVerifier = Callable[[str, str, ExerciseState], bool]


def apply_migration(
    source: LearnerState,
    plan: MigrationPlan,
    source_contract: CurriculumContract,
    destination_contract: CurriculumContract,
    snapshot_verifier: SnapshotVerifier | None = None,
) -> LearnerState:
    """Build and fully validate migrated state without filesystem changes."""
    _validate_contract(source_contract)
    _validate_contract(destination_contract)
    _validate_plan(plan, source_contract, destination_contract)
    try:
        source.validate(
            set(source_contract.ids),
            source_contract.route,
            source_contract.revision,
            source_contract.prerequisites,
            _providers(source_contract),
        )
    except StateError as exc:
        raise MigrationError(f"source state violates its contract: {exc}") from exc

    compatibility = {
        item.destination: item.source for item in plan.compatible_snapshots
    }
    if compatibility and snapshot_verifier is None:
        raise MigrationError("snapshot compatibility requires an explicit verifier")

    histories: dict[str, set[str]] = {}
    for source_id in source_contract.ids:
        destination = _direct_destination(source_id, plan, destination_contract.ids)
        if destination:
            histories.setdefault(destination, set()).add(source_id)
    for merge in plan.history_merges:
        histories.setdefault(merge.destination, set()).update(merge.sources)

    credit_destinations = {
        credit.destination for credit in plan.completion_credits
    }
    completed = {
        destination
        for source_id in source.completed
        if (destination := _direct_destination(
            source_id,
            plan,
            destination_contract.ids,
        ))
        and destination not in credit_destinations
    }
    for credit in plan.completion_credits:
        if set(credit.all_of) <= source.completed:
            completed.add(credit.destination)

    exercises: dict[str, ExerciseState] = {}
    for destination, source_ids in sorted(histories.items()):
        records = tuple(
            (source_id, source.exercises[source_id])
            for source_id in sorted(source_ids)
            if source_id in source.exercises
        )
        if not records:
            continue
        compatible_source = compatibility.get(destination)
        if compatible_source:
            record = source.exercises.get(compatible_source)
            if record is None:
                raise MigrationError(
                    f"compatible snapshot source has no history: {compatible_source}"
                )
            assert snapshot_verifier is not None
            if not snapshot_verifier(compatible_source, destination, record):
                raise MigrationError(
                    "snapshot compatibility verification failed: "
                    f"{compatible_source} -> {destination}"
                )
        exercises[destination] = _merge_history(
            records,
            compatible_source,
            _providers(destination_contract)[destination],
        )

    result = LearnerState(
        destination_contract.revision,
        destination_contract.route,
        completed,
        exercises,
    )
    try:
        result.validate(
            set(destination_contract.ids),
            destination_contract.route,
            destination_contract.revision,
            destination_contract.prerequisites,
            _providers(destination_contract),
        )
    except StateError as exc:
        raise MigrationError(f"migrated state violates destination contract: {exc}") from exc
    return result


def migrate_state_file(
    state_path: Path,
    archive_root: Path,
    plan: MigrationPlan,
    source_contract: CurriculumContract,
    destination_contract: CurriculumContract,
    snapshot_verifier: SnapshotVerifier | None = None,
) -> Path:
    """Lock, plan fully, archive exact original bytes, and atomically replace."""
    state_path = Path(state_path)
    state_parent = _trusted_directory(state_path.parent, "state parent")
    archive_root = Path(archive_root)
    if not archive_root.exists():
        archive_parent = _trusted_directory(archive_root.parent, "archive parent")
        (archive_parent / archive_root.name).mkdir()
    archive_root = _trusted_directory(archive_root, "archive root")

    lock_path = state_parent / f"{state_path.name}.lock"
    flags = os.O_CREAT | os.O_RDWR | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(lock_path, flags, 0o600)
    with os.fdopen(descriptor, "a+b") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
        try:
            original = _read_regular_file(state_parent / state_path.name)
            try:
                source = parse_state(original.decode("utf-8"))
            except (UnicodeDecodeError, StateError) as exc:
                raise MigrationError(f"cannot parse source state: {exc}") from exc
            migrated = apply_migration(
                source,
                plan,
                source_contract,
                destination_contract,
                snapshot_verifier,
            )
            replacement = dump_state(migrated).encode("utf-8")
            digest = hashlib.sha256(original).hexdigest()
            archive = archive_root / f"state-{digest}.toml"
            _write_archive(archive, original)
            _atomic_replace(state_parent / state_path.name, replacement)
            return archive
        finally:
            fcntl.flock(lock.fileno(), fcntl.LOCK_UN)


def _validate_contract(contract: CurriculumContract) -> None:
    probe = LearnerState(contract.revision, contract.route)
    try:
        probe.validate(
            set(contract.ids),
            contract.route,
            contract.revision,
            contract.prerequisites,
            _providers(contract),
        )
    except StateError as exc:
        raise MigrationError(f"invalid curriculum contract: {exc}") from exc


def _validate_plan(
    plan: MigrationPlan,
    source: CurriculumContract,
    destination: CurriculumContract,
) -> None:
    if plan.source_revision != source.revision:
        raise MigrationError("plan source revision does not match source contract")
    if plan.destination_revision != destination.revision:
        raise MigrationError("plan destination revision does not match destination contract")
    if source.revision == destination.revision:
        raise MigrationError("migration revisions must differ")

    source_ids = set(source.ids)
    destination_ids = set(destination.ids)
    referenced_sources = set(plan.renames) | set(plan.removals)
    referenced_sources |= {
        value for credit in plan.completion_credits for value in credit.all_of
    }
    referenced_sources |= {
        value for merge in plan.history_merges for value in merge.sources
    }
    referenced_sources |= {item.source for item in plan.compatible_snapshots}
    unknown = referenced_sources - source_ids
    if unknown:
        raise MigrationError(f"plan references unknown source IDs: {sorted(unknown)}")
    if set(plan.renames) & set(plan.removals):
        raise MigrationError("a source ID cannot be both renamed and removed")
    if set(plan.renames.values()) - destination_ids:
        raise MigrationError("rename destinations must exist")
    if len(set(plan.renames.values())) != len(plan.renames):
        raise MigrationError("renames must be one-to-one; use HistoryMerge for merges")
    unchanged = source_ids & destination_ids
    collisions = set(plan.renames.values()) & (unchanged - set(plan.renames))
    if collisions:
        raise MigrationError(f"rename destinations collide with unchanged IDs: {sorted(collisions)}")
    missing = source_ids - (unchanged | set(plan.renames) | set(plan.removals))
    if missing:
        raise MigrationError(f"source IDs need rename or removal rules: {sorted(missing)}")

    credit_destinations = [item.destination for item in plan.completion_credits]
    if len(credit_destinations) != len(set(credit_destinations)):
        raise MigrationError("completion-credit destinations must be unique")
    for credit in plan.completion_credits:
        if credit.destination not in destination_ids:
            raise MigrationError(f"unknown completion destination: {credit.destination}")
        if not credit.all_of or len(credit.all_of) != len(set(credit.all_of)):
            raise MigrationError("all_of sources must be non-empty and unique")
        direct_sources = {
            source_id
            for source_id in source_ids
            if _direct_destination(source_id, plan, destination.ids)
            == credit.destination
        }
        omitted = direct_sources - set(credit.all_of)
        if omitted:
            raise MigrationError(
                "completion credit must include every direct-mapped source: "
                f"{sorted(omitted)}"
            )

    merge_destinations = [item.destination for item in plan.history_merges]
    if len(merge_destinations) != len(set(merge_destinations)):
        raise MigrationError("history-merge destinations must be unique")
    merged_sources = [
        source_id
        for merge in plan.history_merges
        for source_id in merge.sources
    ]
    if len(merged_sources) != len(set(merged_sources)):
        raise MigrationError("each history source may feed only one history merge")
    for merge in plan.history_merges:
        if merge.destination not in destination_ids:
            raise MigrationError(f"unknown history destination: {merge.destination}")
        if not merge.sources or len(merge.sources) != len(set(merge.sources)):
            raise MigrationError("history sources must be non-empty and unique")
        directly_mapped = {
            source_id
            for source_id in merge.sources
            if _direct_destination(source_id, plan, destination.ids) is not None
        }
        if directly_mapped:
            raise MigrationError(
                "history-merge sources must have no direct destination: "
                f"{sorted(directly_mapped)}"
            )

    histories = {stable_id: set() for stable_id in destination_ids}
    for source_id in source_ids:
        mapped = _direct_destination(source_id, plan, destination.ids)
        if mapped:
            histories[mapped].add(source_id)
    for merge in plan.history_merges:
        histories[merge.destination].update(merge.sources)
    compatible_destinations = [item.destination for item in plan.compatible_snapshots]
    if len(compatible_destinations) != len(set(compatible_destinations)):
        raise MigrationError("snapshot compatibility destinations must be unique")
    for item in plan.compatible_snapshots:
        if item.destination not in destination_ids:
            raise MigrationError(f"unknown snapshot destination: {item.destination}")
        if item.source not in histories[item.destination]:
            raise MigrationError(
                f"snapshot source {item.source!r} does not feed {item.destination!r} history"
            )


def _direct_destination(
    source_id: str,
    plan: MigrationPlan,
    destination_ids: frozenset[str],
) -> str | None:
    if source_id in plan.removals:
        return None
    if source_id in plan.renames:
        return plan.renames[source_id]
    return source_id if source_id in destination_ids else None


def _merge_history(
    records: tuple[tuple[str, ExerciseState], ...],
    compatible_source: str | None,
    destination_provider: str | None,
) -> ExerciseState:
    result = ExerciseState(
        attempts=sum(record.attempts for _, record in records),
        solution_revealed=any(record.solution_revealed for _, record in records),
    )
    if compatible_source:
        compatible = next(
            record for source_id, record in records if source_id == compatible_source
        )
        result.baseline_hash = compatible.baseline_hash
        result.workspace_hash = compatible.workspace_hash
        result.passed_snapshot = compatible.passed_snapshot
        result.baseline_provider = (
            (destination_provider or "") if compatible.baseline_hash else ""
        )
    return result


def _providers(contract: CurriculumContract) -> Mapping[str, str | None]:
    if contract.baseline_providers is None:
        return {stable_id: None for stable_id in contract.ids}
    return contract.baseline_providers


def _trusted_directory(path: Path, label: str) -> Path:
    absolute = Path(os.path.abspath(path))
    try:
        resolved = absolute.resolve(strict=True)
    except OSError as exc:
        raise MigrationError(f"cannot resolve {label}: {absolute}") from exc
    metadata = resolved.stat()
    unsafe_mode = metadata.st_mode & (stat.S_IWGRP | stat.S_IWOTH)
    if resolved != absolute or not resolved.is_dir() or metadata.st_uid != os.getuid() or unsafe_mode:
        raise MigrationError(f"{label} must be private, user-owned, and non-symlinked")
    return resolved


def _read_regular_file(path: Path) -> bytes:
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(path, flags)
    except OSError as exc:
        raise MigrationError(f"cannot safely open state: {path}") from exc
    with os.fdopen(descriptor, "rb") as stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise MigrationError(f"state must be a regular file: {path}")
        return stream.read()


def _write_archive(path: Path, content: bytes) -> None:
    if path.is_symlink():
        raise MigrationError(f"archive must not be a symlink: {path}")
    if path.exists():
        if not path.is_file() or path.read_bytes() != content:
            raise MigrationError(f"existing archive does not match: {path}")
        return
    descriptor, name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        try:
            os.link(temporary, path)
        except FileExistsError:
            if not path.is_file() or path.read_bytes() != content:
                raise MigrationError(f"existing archive does not match: {path}")
        _fsync_directory(path.parent)
    finally:
        if temporary.exists():
            temporary.unlink()


def _atomic_replace(path: Path, content: bytes) -> None:
    descriptor, name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        _fsync_directory(path.parent)
    finally:
        if temporary.exists():
            temporary.unlink()


def _fsync_directory(path: Path) -> None:
    descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
