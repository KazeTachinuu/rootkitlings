"""One-shot migration from the frozen v2 route to the core-route catalog."""

from __future__ import annotations

import fcntl
import hashlib
import os
from pathlib import Path
import tomllib
from typing import Mapping

from engine.migration import (
    CurriculumContract,
    HistoryMerge,
    MigrationError,
    MigrationPlan,
    _atomic_replace,
    _read_regular_file,
    _trusted_directory,
    _write_archive,
    apply_migration,
    parse_state_for_contract,
)
from engine.state import dump_state
from engine.workspace import (
    WorkspaceError,
    _scan_regular_tree,
    archive_and_remove_workspaces,
)


SOURCE_REVISION = "2026.10-catalog-v2"
DESTINATION_REVISION = "2026.10-core-route"

SOURCE_ROUTE = (
    "module-lifecycle", "module-build", "multi-file-module", "kernel-debugging",
    "init-failure", "module-parameter", "parameter-callback", "kernel-allocation",
    "initialization-rollback", "proc-entry", "proc-read", "proc-write",
    "error-pointer", "kernel-list", "character-device", "ioctl-dispatch",
    "usermode-helper", "kernel-timer", "workqueue", "kernel-thread",
    "atomic-counter", "completion", "mutex-invariant", "wait-queue",
    "live-config-store", "beacon-scheduler", "dead-man-watchdog", "current-task",
    "task-credentials", "credential-replacement", "symbol-resolution",
    "kernel-randomness", "one-time-root-token", "kprobe", "kretprobe", "fprobe",
    "syscall-argument-decoding", "hook-rollback", "file-tripwire",
    "module-list-hiding", "sysfs-artifact", "directory-filtering",
    "read-view-filtering", "log-view-filtering", "socket-row-filtering",
    "ghost-process", "sealed-dead-drop", "netfilter-hook", "packet-classification",
    "icmp-trigger", "kernel-socket", "packet-redirection", "tcp-root-shell",
    "icmp-messaging", "dns-wire-encoding", "connect-back-agent",
    "kernel-file-transaction", "boot-time-module-request", "xor-obfuscation",
    "authenticated-encryption", "sha256-verification", "hmac-authentication",
    "keyed-domain-generation", "authenticated-dns-signal", "operation-glasshouse",
)

SOURCE_DIRECT_PREREQUISITES = {
    "module-lifecycle": (),
    "module-build": ("module-lifecycle",),
    "multi-file-module": ("module-build",),
    "kernel-debugging": ("module-build",),
    "init-failure": ("module-lifecycle",),
    "module-parameter": ("module-lifecycle",),
    "parameter-callback": ("module-parameter",),
    "kernel-allocation": ("module-lifecycle",),
    "initialization-rollback": ("init-failure", "kernel-allocation"),
    "proc-entry": ("module-lifecycle",),
    "proc-read": ("proc-entry",),
    "proc-write": ("proc-read",),
    "error-pointer": ("init-failure",),
    "kernel-list": ("kernel-allocation", "proc-write"),
    "character-device": ("module-lifecycle",),
    "ioctl-dispatch": ("character-device",),
    "usermode-helper": ("proc-write",),
    "kernel-timer": (),
    "workqueue": ("kernel-timer",),
    "kernel-thread": ("module-lifecycle",),
    "atomic-counter": ("kernel-thread",),
    "completion": ("kernel-thread",),
    "mutex-invariant": ("proc-write", "kernel-thread"),
    "wait-queue": ("kernel-thread",),
    "live-config-store": ("kernel-list", "mutex-invariant"),
    "beacon-scheduler": ("parameter-callback", "workqueue"),
    "dead-man-watchdog": ("atomic-counter", "mutex-invariant", "wait-queue"),
    "current-task": ("proc-read",),
    "task-credentials": ("current-task",),
    "credential-replacement": ("proc-write", "task-credentials"),
    "symbol-resolution": (),
    "kernel-randomness": (),
    "one-time-root-token": (
        "mutex-invariant", "credential-replacement", "kernel-randomness",
    ),
    "kprobe": ("symbol-resolution",),
    "kretprobe": ("kprobe",),
    "fprobe": ("kprobe",),
    "syscall-argument-decoding": ("proc-write", "kprobe"),
    "hook-rollback": ("initialization-rollback", "kprobe"),
    "file-tripwire": ("task-credentials", "kprobe"),
    "module-list-hiding": ("kernel-list",),
    "sysfs-artifact": ("initialization-rollback",),
    "directory-filtering": ("kretprobe", "syscall-argument-decoding"),
    "read-view-filtering": ("kretprobe",),
    "log-view-filtering": ("kretprobe",),
    "socket-row-filtering": ("kretprobe",),
    "ghost-process": ("kernel-allocation", "kernel-thread", "directory-filtering"),
    "sealed-dead-drop": ("directory-filtering",),
    "netfilter-hook": ("module-lifecycle",),
    "packet-classification": ("netfilter-hook",),
    "icmp-trigger": ("packet-classification",),
    "kernel-socket": ("module-lifecycle",),
    "packet-redirection": ("packet-classification",),
    "tcp-root-shell": ("usermode-helper", "kernel-socket"),
    "icmp-messaging": ("icmp-trigger",),
    "dns-wire-encoding": (),
    "connect-back-agent": ("wait-queue", "kernel-socket"),
    "kernel-file-transaction": ("initialization-rollback",),
    "boot-time-module-request": ("kernel-file-transaction",),
    "xor-obfuscation": (),
    "authenticated-encryption": ("kernel-allocation",),
    "sha256-verification": ("error-pointer", "authenticated-encryption"),
    "hmac-authentication": ("initialization-rollback", "sha256-verification"),
    "keyed-domain-generation": (),
    "authenticated-dns-signal": (
        "dns-wire-encoding", "hmac-authentication", "keyed-domain-generation",
    ),
    "operation-glasshouse": (
        "boot-time-module-request", "credential-replacement",
        "module-list-hiding", "directory-filtering",
    ),
}

SOURCE_BASELINE_PROVIDERS = {
    stable_id: None for stable_id in SOURCE_ROUTE
}
SOURCE_BASELINE_PROVIDERS.update({
    "module-build": "module-lifecycle",
    "init-failure": "module-build",
    "module-parameter": "init-failure",
    "parameter-callback": "module-parameter",
    "kernel-allocation": "parameter-callback",
    "initialization-rollback": "kernel-allocation",
})

DESTINATION_ROUTE = (
    "module-build-lifecycle", "precise-init-failure", "owned-kernel-state",
    "proc-publication", "proc-read-contract", "bounded-control-switch",
    "timer-workqueue-scheduler", "event-driven-kernel-thread",
    "controlled-resident-core", "current-task-attribution",
    "linux-credential-model", "transactional-credential-replacement",
    "one-use-authorization", "managed-kprobe", "return-aware-syscall-telemetry",
    "authorized-actions-telemetry", "cross-view-evidence",
    "packed-directory-filter", "bounded-rendered-filter",
    "concealment-evidence-map", "hostile-packet-lengths", "safe-skb-access",
    "netfilter-observer", "validated-packet-classification",
    "writable-packet-mutation", "kernel-socket-ownership",
    "tcp-framing-cancellation", "bounded-dns-wire-name",
    "guarded-file-transaction", "remote-resident-agent",
    "protection-property-selection", "sha256-verification",
    "authenticated-replay-safe-message", "authenticated-encryption",
    "authenticated-dns-signal", "authenticated-resident-agent",
)

DESTINATION_DIRECT_PREREQUISITES = {
    "module-build-lifecycle": (),
    "precise-init-failure": ("module-build-lifecycle",),
    "owned-kernel-state": ("precise-init-failure",),
    "proc-publication": ("owned-kernel-state",),
    "proc-read-contract": ("proc-publication",),
    "bounded-control-switch": ("proc-read-contract",),
    "timer-workqueue-scheduler": ("bounded-control-switch",),
    "event-driven-kernel-thread": ("bounded-control-switch",),
    "controlled-resident-core": (
        "timer-workqueue-scheduler", "event-driven-kernel-thread",
    ),
    "current-task-attribution": ("proc-read-contract",),
    "linux-credential-model": ("current-task-attribution",),
    "transactional-credential-replacement": (
        "bounded-control-switch", "linux-credential-model",
    ),
    "one-use-authorization": ("transactional-credential-replacement",),
    "managed-kprobe": ("bounded-control-switch",),
    "return-aware-syscall-telemetry": ("managed-kprobe",),
    "authorized-actions-telemetry": (
        "controlled-resident-core", "one-use-authorization",
        "return-aware-syscall-telemetry",
    ),
    "cross-view-evidence": ("managed-kprobe",),
    "packed-directory-filter": (
        "return-aware-syscall-telemetry", "cross-view-evidence",
    ),
    "bounded-rendered-filter": (
        "return-aware-syscall-telemetry", "cross-view-evidence",
    ),
    "concealment-evidence-map": (
        "authorized-actions-telemetry", "packed-directory-filter",
    ),
    "hostile-packet-lengths": (),
    "safe-skb-access": ("hostile-packet-lengths",),
    "netfilter-observer": ("proc-publication",),
    "validated-packet-classification": (
        "netfilter-observer", "safe-skb-access",
    ),
    "writable-packet-mutation": ("validated-packet-classification",),
    "kernel-socket-ownership": ("proc-publication",),
    "tcp-framing-cancellation": (
        "event-driven-kernel-thread", "kernel-socket-ownership",
    ),
    "bounded-dns-wire-name": ("hostile-packet-lengths",),
    "guarded-file-transaction": ("proc-publication",),
    "remote-resident-agent": (
        "concealment-evidence-map", "tcp-framing-cancellation",
        "guarded-file-transaction",
    ),
    "protection-property-selection": ("bounded-dns-wire-name",),
    "sha256-verification": ("protection-property-selection",),
    "authenticated-replay-safe-message": (
        "one-use-authorization", "sha256-verification",
    ),
    "authenticated-encryption": ("authenticated-replay-safe-message",),
    "authenticated-dns-signal": (
        "cross-view-evidence", "kernel-socket-ownership",
        "authenticated-replay-safe-message",
    ),
    "authenticated-resident-agent": (
        "remote-resident-agent", "authenticated-encryption",
        "authenticated-dns-signal",
    ),
}

DESTINATION_BASELINE_PROVIDERS = {
    stable_id: None for stable_id in DESTINATION_ROUTE
}
DESTINATION_BASELINE_PROVIDERS.update({
    "precise-init-failure": "module-build-lifecycle",
    "owned-kernel-state": "precise-init-failure",
    "proc-publication": "owned-kernel-state",
    "controlled-resident-core": "proc-publication",
    "authorized-actions-telemetry": "controlled-resident-core",
    "concealment-evidence-map": "authorized-actions-telemetry",
    "remote-resident-agent": "concealment-evidence-map",
    "authenticated-resident-agent": "remote-resident-agent",
})


def _completion_dependencies(
    route: tuple[str, ...],
    direct: Mapping[str, tuple[str, ...]],
    providers: Mapping[str, str | None],
) -> dict[str, frozenset[str]]:
    result: dict[str, frozenset[str]] = {}
    for stable_id in route:
        dependencies = set(direct[stable_id])
        if providers[stable_id]:
            dependencies.add(providers[stable_id])
        result[stable_id] = frozenset(dependencies)
    return result


SOURCE_CONTRACT = CurriculumContract(
    SOURCE_REVISION,
    frozenset(SOURCE_ROUTE),
    SOURCE_ROUTE,
    _completion_dependencies(
        SOURCE_ROUTE, SOURCE_DIRECT_PREREQUISITES, SOURCE_BASELINE_PROVIDERS,
    ),
    SOURCE_BASELINE_PROVIDERS,
)
DESTINATION_CONTRACT = CurriculumContract(
    DESTINATION_REVISION,
    frozenset(DESTINATION_ROUTE),
    DESTINATION_ROUTE,
    _completion_dependencies(
        DESTINATION_ROUTE,
        DESTINATION_DIRECT_PREREQUISITES,
        DESTINATION_BASELINE_PROVIDERS,
    ),
    DESTINATION_BASELINE_PROVIDERS,
)

# Only direct, one-to-one refactors retain attempts/reveal history. Merges, splits,
# retirements and optional-track moves remain available solely in the state archive.
HISTORY_DESTINATIONS = {
    "init-failure": "precise-init-failure",
    "proc-entry": "proc-publication",
    "proc-read": "proc-read-contract",
    "proc-write": "bounded-control-switch",
    "current-task": "current-task-attribution",
    "task-credentials": "linux-credential-model",
    "credential-replacement": "transactional-credential-replacement",
    "directory-filtering": "packed-directory-filter",
    "read-view-filtering": "bounded-rendered-filter",
    "ghost-process": "concealment-evidence-map",
    "netfilter-hook": "netfilter-observer",
    "packet-redirection": "writable-packet-mutation",
    "dns-wire-encoding": "bounded-dns-wire-name",
    "kernel-file-transaction": "guarded-file-transaction",
    "sha256-verification": "sha256-verification",
    "hmac-authentication": "authenticated-replay-safe-message",
    "authenticated-dns-signal": "authenticated-dns-signal",
    "operation-glasshouse": "authenticated-resident-agent",
}

ROUTE_PLAN = MigrationPlan(
    SOURCE_REVISION,
    DESTINATION_REVISION,
    {},
    frozenset(SOURCE_ROUTE),
    history_merges=tuple(
        HistoryMerge(destination, (source,))
        for source, destination in HISTORY_DESTINATIONS.items()
    ),
)


def active_destination_contract(
    revision: str,
    ids: set[str],
    route: tuple[str, ...],
    prerequisites: Mapping[str, frozenset[str]],
    baseline_providers: Mapping[str, str | None],
) -> CurriculumContract:
    """Build the catalog contract passed by the CLI at the cutover boundary."""
    return CurriculumContract(
        revision,
        frozenset(ids),
        tuple(route),
        {stable_id: frozenset(values) for stable_id, values in prerequisites.items()},
        dict(baseline_providers),
    )


def migrate_core_route(
    state_path: Path,
    work_root: Path,
    archive_root: Path,
    active_contract: CurriculumContract,
) -> bool:
    """Archive v2 state/workspaces and atomically install an empty-credit v3 state."""
    if active_contract != DESTINATION_CONTRACT:
        raise MigrationError(
            "active catalog does not match the frozen core-route destination contract"
        )

    state_path = Path(state_path)
    if state_path.is_symlink():
        raise MigrationError(f"state must not be a symlink: {state_path}")
    if not state_path.exists():
        return False

    state_parent = _trusted_directory(state_path.parent, "state parent")
    preflight = _read_regular_file(state_parent / state_path.name)
    preflight_revision = _state_revision(preflight)
    if preflight_revision == DESTINATION_REVISION:
        parse_state_for_contract(preflight, DESTINATION_CONTRACT)
        return False
    if preflight_revision != SOURCE_REVISION:
        raise MigrationError(
            f"no route migration exists for revision {preflight_revision!r}"
        )

    work_root = _trusted_directory(Path(work_root), "workspace root")
    archive_root = _trusted_directory(Path(archive_root), "archive root")
    lock_path = state_parent / f"{state_path.name}.lock"
    flags = os.O_CREAT | os.O_RDWR | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(lock_path, flags, 0o600)
    with os.fdopen(descriptor, "a+b") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
        try:
            original = _read_regular_file(state_parent / state_path.name)
            revision = _state_revision(original)
            if revision == DESTINATION_REVISION:
                parse_state_for_contract(original, DESTINATION_CONTRACT)
                return False
            if revision != SOURCE_REVISION:
                raise MigrationError(
                    f"no route migration exists for revision {revision!r}"
                )

            source = parse_state_for_contract(original, SOURCE_CONTRACT)
            migrated = apply_migration(
                source, ROUTE_PLAN, SOURCE_CONTRACT, DESTINATION_CONTRACT,
            )
            if migrated.completed:
                raise MigrationError("route migration must not grant completion credit")
            for record in migrated.exercises.values():
                if record.baseline_hash or record.workspace_hash or record.passed_snapshot:
                    raise MigrationError("route migration must not retain content hashes")

            _preflight_workspaces(work_root)
            digest = hashlib.sha256(original).hexdigest()
            archive = archive_root / f"state-route-v2-{digest}.toml"
            _write_archive(archive, original)
            try:
                archive_and_remove_workspaces(
                    work_root, archive_root, SOURCE_CONTRACT.ids,
                )
            except WorkspaceError as exc:
                raise MigrationError(f"cannot archive old-route workspaces: {exc}") from exc
            _atomic_replace(
                state_parent / state_path.name,
                dump_state(migrated).encode("utf-8"),
            )
            return True
        finally:
            fcntl.flock(lock.fileno(), fcntl.LOCK_UN)


def _state_revision(content: bytes) -> str:
    try:
        raw = tomllib.loads(content.decode("utf-8"))
    except (UnicodeDecodeError, tomllib.TOMLDecodeError) as exc:
        raise MigrationError(f"cannot parse source state: {exc}") from exc
    revision = raw.get("curriculum_revision")
    if not isinstance(revision, str):
        raise MigrationError("source state curriculum_revision must be a string")
    return revision


def _preflight_workspaces(work_root: Path) -> None:
    """Reject unsafe old workspace trees before publishing any workspace archive."""
    try:
        for stable_id in SOURCE_ROUTE:
            workspace = work_root / stable_id
            if workspace.is_symlink():
                raise WorkspaceError(f"workspace must not be a symlink: {workspace}")
            if not workspace.exists():
                continue
            if not workspace.is_dir():
                raise WorkspaceError(f"workspace must be a directory: {workspace}")
            _scan_regular_tree(workspace)
    except WorkspaceError as exc:
        raise MigrationError(f"cannot archive old-route workspaces: {exc}") from exc
