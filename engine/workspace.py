"""Safe deterministic learner workspace, archive, and snapshot primitives."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import tempfile
import tomllib
from typing import Iterable, Mapping


ID_RE = re.compile(r"[a-z0-9][a-z0-9-]*\Z")
MANIFEST = "snapshot.toml"


class WorkspaceError(ValueError):
    pass


def _real_directory(path: Path, label: str) -> Path:
    path = Path(path)
    if path.is_symlink():
        raise WorkspaceError(f"{label} must not be a symlink")
    try:
        resolved = path.resolve(strict=True)
    except OSError as exc:
        raise WorkspaceError(f"cannot resolve {label}: {path}") from exc
    if not resolved.is_dir():
        raise WorkspaceError(f"{label} must be a directory: {path}")
    return resolved


def _relative(value: str) -> PurePosixPath:
    path = PurePosixPath(value)
    if path.is_absolute() or not path.parts or any(part in {"", ".", ".."} for part in path.parts):
        raise WorkspaceError(f"unsafe relative path: {value!r}")
    return path


def _stable_id(value: str) -> str:
    if not ID_RE.fullmatch(value):
        raise WorkspaceError(f"invalid stable ID: {value!r}")
    return value


def _contained(root: Path, relative: PurePosixPath) -> Path:
    root = root.resolve(strict=True)
    candidate = root.joinpath(*relative.parts)
    try:
        candidate.resolve(strict=False).relative_to(root)
    except ValueError as exc:
        raise WorkspaceError(f"path escapes root: {relative}") from exc
    return candidate


def _regular_file(root: Path, relative: PurePosixPath) -> Path:
    candidate = _contained(root, relative)
    cursor = root.resolve(strict=True)
    for part in relative.parts:
        cursor = cursor / part
        if cursor.is_symlink():
            raise WorkspaceError(f"symlink is not allowed: {relative}")
    if not candidate.is_file() or candidate.is_symlink():
        raise WorkspaceError(f"declared path is not a regular file: {relative}")
    return candidate


def _declared(paths: Iterable[str]) -> tuple[PurePosixPath, ...]:
    result = tuple(sorted((_relative(value) for value in paths), key=lambda item: item.as_posix()))
    if not result or len(result) != len(set(result)):
        raise WorkspaceError("declared files must be non-empty and unique")
    return result


def canonical_tree_hash(root: Path, declared_files: Iterable[str]) -> str:
    """Hash sorted relative names and bytes with unambiguous length framing."""
    root = _real_directory(Path(root), "tree root")
    paths = _declared(declared_files)
    digest = hashlib.sha256(b"rootkitlings-tree-v1\0")
    for relative in paths:
        source = _regular_file(root, relative)
        name = relative.as_posix().encode("utf-8")
        data = source.read_bytes()
        executable = 1 if source.stat().st_mode & 0o111 else 0
        digest.update(len(name).to_bytes(8, "big"))
        digest.update(name)
        digest.update(bytes((executable,)))
        digest.update(len(data).to_bytes(8, "big"))
        digest.update(data)
    return digest.hexdigest()


def _copy_declared(
    source: Path,
    destination: Path,
    paths: tuple[PurePosixPath, ...],
    *,
    learner_writable: bool = False,
) -> None:
    for relative in paths:
        source_file = _regular_file(source, relative)
        target = destination.joinpath(*relative.parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(source_file.read_bytes())
        executable = bool(source_file.stat().st_mode & 0o111)
        if learner_writable:
            target.chmod(0o755 if executable else 0o644)
        else:
            target.chmod(source_file.stat().st_mode & 0o777)


def materialize(starter: Path, work_root: Path, stable_id: str, declared_files: Iterable[str]) -> Path:
    """Atomically create one workspace from an immutable starter."""
    stable_id = _stable_id(stable_id)
    starter = _real_directory(Path(starter), "starter")
    work_root = _real_directory(Path(work_root), "workspace root")
    destination = work_root / stable_id
    if destination.exists() or destination.is_symlink():
        raise WorkspaceError(f"workspace already exists: {destination}")
    paths = _declared(declared_files)
    temporary = Path(tempfile.mkdtemp(prefix=f".{stable_id}.", dir=work_root))
    try:
        _copy_declared(starter, temporary, paths, learner_writable=True)
        os.replace(temporary, destination)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    return destination


def materialize_imported(
    starter: Path,
    work_root: Path,
    stable_id: str,
    starter_files: Iterable[str],
    consumer_files: Iterable[str],
    imported_files: Mapping[str, tuple[bytes, int]],
) -> Path:
    """Create a workspace from local starter files and reference imports."""
    stable_id = _stable_id(stable_id)
    starter = _real_directory(Path(starter), "starter")
    work_root = _real_directory(Path(work_root), "workspace root")
    local = _declared(starter_files)
    declared = _declared(consumer_files)
    imported = _validated_imports(
        imported_files,
        declared,
        {path.as_posix() for path in local},
    )
    if {path.as_posix() for path in local} | set(imported) != {
        path.as_posix() for path in declared
    }:
        raise WorkspaceError(
            "starter files and imports do not cover the declared workspace"
        )
    destination = work_root / stable_id
    if destination.exists() or destination.is_symlink():
        raise WorkspaceError(f"workspace already exists: {destination}")
    temporary = Path(tempfile.mkdtemp(prefix=f".{stable_id}.", dir=work_root))
    try:
        _copy_declared(starter, temporary, local, learner_writable=True)
        _write_imports(temporary, imported)
        os.replace(temporary, destination)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    return destination


def materialize_inherited(
    snapshot: Path,
    additions: Path | None,
    work_root: Path,
    stable_id: str,
    provider_files: Iterable[str],
    consumer_files: Iterable[str],
    imported_files: Mapping[str, tuple[bytes, int]] | None = None,
) -> Path:
    """Combine a learner snapshot with local additions and reference imports."""
    stable_id = _stable_id(stable_id)
    snapshot = _real_directory(Path(snapshot), "provider snapshot")
    work_root = _real_directory(Path(work_root), "workspace root")
    verify_snapshot(snapshot)
    inherited = _declared(provider_files)
    declared = _declared(consumer_files)
    inherited_set = set(inherited)
    declared_set = set(declared)
    snapshot_files = set(_scan_regular_tree(snapshot)) - {MANIFEST}
    if snapshot_files != {path.as_posix() for path in inherited}:
        raise WorkspaceError(
            "provider snapshot does not exactly match provider declared files"
        )
    imports = _validated_imports(
        imported_files or {},
        declared,
        {path.as_posix() for path in inherited},
    )
    if not inherited_set < declared_set:
        raise WorkspaceError(
            "inherited materialization requires a strict additive file set"
        )
    new_paths = tuple(sorted(
        declared_set - inherited_set - {PurePosixPath(path) for path in imports},
        key=lambda item: item.as_posix(),
    ))
    expected_additions = {path.as_posix() for path in new_paths}
    if additions is None:
        if expected_additions:
            raise WorkspaceError("local additions do not cover the declared workspace")
    else:
        additions = _real_directory(Path(additions), "additions")
        _validate_additions_tree(additions, expected_additions)
    destination = work_root / stable_id
    if destination.exists() or destination.is_symlink():
        raise WorkspaceError(f"workspace already exists: {destination}")
    temporary = Path(tempfile.mkdtemp(prefix=f".{stable_id}.", dir=work_root))
    try:
        _copy_declared(snapshot, temporary, inherited, learner_writable=True)
        if additions is not None:
            _copy_declared(additions, temporary, new_paths, learner_writable=True)
        _write_imports(temporary, imports)
        os.replace(temporary, destination)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    return destination


def _validated_imports(
    imported_files: Mapping[str, tuple[bytes, int]],
    declared: tuple[PurePosixPath, ...],
    occupied: set[str],
) -> dict[str, tuple[bytes, int]]:
    declared_set = {path.as_posix() for path in declared}
    result: dict[str, tuple[bytes, int]] = {}
    for value, imported in imported_files.items():
        relative = _relative(value).as_posix()
        if relative not in declared_set:
            raise WorkspaceError(f"import destination is not declared: {relative}")
        if relative in occupied or relative in result:
            raise WorkspaceError(f"import destination collides: {relative}")
        data, mode = imported
        if not isinstance(data, bytes) or mode not in {0o644, 0o755}:
            raise WorkspaceError(f"invalid imported file: {relative}")
        result[relative] = (data, mode)
    return result


def _write_imports(
    destination: Path,
    imported_files: Mapping[str, tuple[bytes, int]],
) -> None:
    for value, (data, mode) in sorted(imported_files.items()):
        target = destination.joinpath(*PurePosixPath(value).parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        target.chmod(mode)


def _scan_regular_tree(root: Path) -> tuple[str, ...]:
    root = _real_directory(Path(root), "tree root")
    files: list[str] = []
    for directory, names, filenames in os.walk(root, followlinks=False):
        directory_path = Path(directory)
        for name in names:
            path = directory_path / name
            if path.is_symlink() or not path.is_dir():
                raise WorkspaceError(f"workspace contains a symlink or special directory: {path}")
        for name in filenames:
            path = directory_path / name
            if path.is_symlink() or not path.is_file():
                raise WorkspaceError(f"workspace contains a symlink or special file: {path}")
            files.append(path.relative_to(root).as_posix())
    return tuple(sorted(files))


def _validate_additions_tree(root: Path, expected_files: set[str]) -> None:
    actual_files = set(_scan_regular_tree(root))
    actual_directories: set[str] = set()
    for directory, names, _ in os.walk(root, followlinks=False):
        directory_path = Path(directory)
        for name in names:
            actual_directories.add(
                (directory_path / name).relative_to(root).as_posix()
            )
    expected_directories = {
        parent.as_posix()
        for value in expected_files
        for parent in PurePosixPath(value).parents
        if parent != PurePosixPath(".")
    }
    if actual_files != expected_files or actual_directories != expected_directories:
        raise WorkspaceError(
            "additions tree must contain exactly the new files and their parent directories"
        )


def archive_workspace(workspace: Path, archive_root: Path, stable_id: str) -> Path:
    """Copy a safe workspace to a deterministic content-addressed archive."""
    stable_id = _stable_id(stable_id)
    workspace = _real_directory(Path(workspace), "workspace")
    archive_root = _real_directory(Path(archive_root), "archive root")
    files = _scan_regular_tree(workspace)
    if not files:
        raise WorkspaceError("cannot archive an empty workspace")
    content_hash = canonical_tree_hash(workspace, files)
    destination = archive_root / f"{stable_id}-{content_hash}"
    if destination.is_symlink():
        raise WorkspaceError(f"archive destination must not be a symlink: {destination}")
    if destination.exists():
        if set(_scan_regular_tree(destination)) != set(files):
            raise WorkspaceError(f"existing archive contains missing or undeclared files: {destination}")
        if canonical_tree_hash(destination, files) != content_hash:
            raise WorkspaceError(f"existing archive does not match its name: {destination}")
        return destination
    temporary = Path(tempfile.mkdtemp(prefix=f".{stable_id}.", dir=archive_root))
    try:
        _copy_declared(workspace, temporary, tuple(PurePosixPath(item) for item in files))
        os.replace(temporary, destination)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    return destination


def _replace_workspace(source: Path, work_root: Path, stable_id: str, declared_files: Iterable[str]) -> Path:
    source = _real_directory(Path(source), "restore source")
    work_root = _real_directory(Path(work_root), "workspace root")
    destination = work_root / _stable_id(stable_id)
    if destination.is_symlink() or (destination.exists() and not destination.is_dir()):
        raise WorkspaceError(f"workspace destination must be a real directory: {destination}")
    paths = _declared(declared_files)
    temporary = Path(tempfile.mkdtemp(prefix=f".{stable_id}.restore.", dir=work_root))
    previous = None
    try:
        _copy_declared(source, temporary, paths, learner_writable=True)
        if destination.exists():
            previous = work_root / f".{stable_id}.previous.{os.getpid()}"
            os.replace(destination, previous)
        os.replace(temporary, destination)
        if previous is not None:
            shutil.rmtree(previous)
    except Exception:
        if previous is not None and previous.exists() and not destination.exists():
            os.replace(previous, destination)
        raise
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    return destination


def restore_workspace(work_root: Path, archive_root: Path, stable_id: str, baseline: Path,
                      declared_files: Iterable[str]) -> tuple[Path, Path]:
    """Archive current work, then restore its recorded starting baseline."""
    work_root = _real_directory(Path(work_root), "workspace root")
    workspace = work_root / _stable_id(stable_id)
    archive = archive_workspace(workspace, archive_root, stable_id)
    return _replace_workspace(baseline, work_root, stable_id, declared_files), archive


def archive_and_remove_workspaces(work_root: Path, archive_root: Path,
                                  stable_ids: Iterable[str]) -> dict[str, Path]:
    """Archive every present workspace, then remove them as one recoverable set."""
    work_root = _real_directory(Path(work_root), "workspace root")
    archive_root = _real_directory(Path(archive_root), "archive root")
    stable_ids = tuple(sorted({_stable_id(value) for value in stable_ids}))
    workspaces: dict[str, Path] = {}
    archives: dict[str, Path] = {}
    for stable_id in stable_ids:
        workspace = work_root / stable_id
        if workspace.is_symlink():
            raise WorkspaceError(f"workspace must not be a symlink: {workspace}")
        if not workspace.exists():
            continue
        if not workspace.is_dir():
            raise WorkspaceError(f"workspace must be a directory: {workspace}")
        workspaces[stable_id] = workspace
        archives[stable_id] = archive_workspace(workspace, archive_root, stable_id)
    for workspace in workspaces.values():
        _scan_regular_tree(workspace)
    for workspace in workspaces.values():
        shutil.rmtree(workspace)
    return archives


def clean_paths(workspace: Path, generated_paths: Iterable[str]) -> None:
    """Remove only explicitly declared build outputs inside a safe workspace."""
    workspace = _real_directory(Path(workspace), "workspace")
    for relative in sorted((_relative(value) for value in generated_paths), key=lambda item: len(item.parts), reverse=True):
        lexical_target = workspace.joinpath(*relative.parts)
        if lexical_target.is_symlink():
            raise WorkspaceError(f"refusing to clean symlink: {relative}")
        target = _contained(workspace, relative)
        if not target.exists() and not target.is_symlink():
            continue
        if target.is_dir():
            _scan_regular_tree(target)
            shutil.rmtree(target)
        elif target.is_file():
            target.unlink()
        else:
            raise WorkspaceError(f"refusing to clean special file: {relative}")


def create_snapshot(workspace: Path, passed_root: Path, declared_files: Iterable[str]) -> tuple[str, Path]:
    """Create or verify an immutable content-addressed snapshot and manifest."""
    workspace = _real_directory(Path(workspace), "workspace")
    passed_root = _real_directory(Path(passed_root), "passed root")
    paths = _declared(declared_files)
    content_hash = canonical_tree_hash(workspace, (path.as_posix() for path in paths))
    destination = passed_root / content_hash
    if destination.is_symlink():
        raise WorkspaceError(f"snapshot destination must not be a symlink: {destination}")
    if destination.exists():
        verify_snapshot(destination, content_hash)
        return content_hash, destination
    temporary = Path(tempfile.mkdtemp(prefix=f".{content_hash}.", dir=passed_root))
    try:
        _copy_declared(workspace, temporary, paths)
        records = []
        for relative in paths:
            data = _regular_file(workspace, relative).read_bytes()
            mode = "executable" if _regular_file(workspace, relative).stat().st_mode & 0o111 else "regular"
            records.append((relative.as_posix(), len(data), hashlib.sha256(data).hexdigest(), mode))
        (temporary / MANIFEST).write_text(_manifest(content_hash, records), encoding="utf-8", newline="\n")
        for path in temporary.rglob("*"):
            if path.is_dir():
                path.chmod(0o555)
            else:
                path.chmod(0o555 if path.stat().st_mode & 0o111 else 0o444)
        temporary.chmod(0o555)
        os.replace(temporary, destination)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    verify_snapshot(destination, content_hash)
    return content_hash, destination


def _manifest(content_hash: str, records: list[tuple[str, int, str, str]]) -> str:
    lines = ['algorithm = "sha256"', f'tree_hash = "{content_hash}"']
    for path, size, digest, mode in records:
        escaped = path.replace("\\", "\\\\").replace('"', '\\"')
        lines += ["", "[[files]]", f'path = "{escaped}"', f"size = {size}", f'sha256 = "{digest}"', f'mode = "{mode}"']
    return "\n".join(lines) + "\n"


def verify_snapshot(snapshot: Path, expected_hash: str | None = None) -> str:
    snapshot = _real_directory(Path(snapshot), "snapshot")
    manifest_path = _regular_file(snapshot, PurePosixPath(MANIFEST))
    try:
        data = tomllib.loads(manifest_path.read_text(encoding="utf-8"))
    except tomllib.TOMLDecodeError as exc:
        raise WorkspaceError(f"invalid snapshot manifest: {exc}") from exc
    if set(data) != {"algorithm", "tree_hash", "files"} or data["algorithm"] != "sha256":
        raise WorkspaceError("snapshot manifest fields or algorithm are invalid")
    tree_hash = data["tree_hash"]
    if not isinstance(tree_hash, str) or not re.fullmatch(r"[0-9a-f]{64}", tree_hash):
        raise WorkspaceError("snapshot tree hash is invalid")
    if expected_hash is not None and tree_hash != expected_hash:
        raise WorkspaceError("snapshot hash does not match expected hash")
    records = data["files"]
    if not isinstance(records, list) or not records:
        raise WorkspaceError("snapshot manifest must contain files")
    paths: list[str] = []
    for record in records:
        if not isinstance(record, dict) or set(record) != {"path", "size", "sha256", "mode"}:
            raise WorkspaceError("snapshot file record is malformed")
        relative = _relative(record["path"] if isinstance(record["path"], str) else "")
        file = _regular_file(snapshot, relative)
        content = file.read_bytes()
        if record["size"] != len(content) or record["sha256"] != hashlib.sha256(content).hexdigest():
            raise WorkspaceError(f"snapshot file verification failed: {relative}")
        expected_mode = "executable" if file.stat().st_mode & 0o111 else "regular"
        if record["mode"] not in {"regular", "executable"} or record["mode"] != expected_mode:
            raise WorkspaceError(f"snapshot file mode verification failed: {relative}")
        paths.append(relative.as_posix())
    if tuple(paths) != tuple(sorted(set(paths))):
        raise WorkspaceError("snapshot paths must be sorted and unique")
    if canonical_tree_hash(snapshot, paths) != tree_hash:
        raise WorkspaceError("snapshot tree hash verification failed")
    expected_tree = set(paths) | {MANIFEST}
    if set(_scan_regular_tree(snapshot)) != expected_tree:
        raise WorkspaceError("snapshot contains undeclared files")
    return tree_hash
