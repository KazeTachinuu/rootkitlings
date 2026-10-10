"""Validate, compose, and safely materialize reference solution trees.

Solutions combine local starter files, sparse overlays, and explicit files from
prerequisite solutions. Composition always produces one complete declared tree.
"""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path, PurePosixPath
import shutil
import tempfile
from typing import Iterable, Mapping

from .workspace import WorkspaceError, _real_directory, _relative, _safe_name


@dataclass(frozen=True)
class SolutionFile:
    data: bytes
    mode: int


SolutionTree = dict[str, SolutionFile]
SolutionImport = tuple[
    str,
    Mapping[str, SolutionFile],
    Iterable[tuple[str, str]],
]


def _declared(paths: Iterable[str]) -> set[str]:
    result = {_relative(value).as_posix() for value in paths}
    if not result:
        raise WorkspaceError("solution declared files must not be empty")
    return result


def _read_tree(root: Path, label: str) -> SolutionTree:
    root = _real_directory(Path(root), label)
    files: SolutionTree = {}
    directories: set[str] = set()
    for directory, names, filenames in os.walk(root, followlinks=False):
        directory_path = Path(directory)
        for name in names:
            path = directory_path / name
            if path.is_symlink() or not path.is_dir():
                raise WorkspaceError(f"{label} contains a symlink or special directory: {path}")
            directories.add(path.relative_to(root).as_posix())
        for name in filenames:
            path = directory_path / name
            if path.is_symlink() or not path.is_file():
                raise WorkspaceError(f"{label} contains a symlink or special file: {path}")
            relative = path.relative_to(root).as_posix()
            mode = 0o755 if path.stat().st_mode & 0o111 else 0o644
            files[relative] = SolutionFile(path.read_bytes(), mode)
    expected_directories = {
        parent.as_posix()
        for value in files
        for parent in PurePosixPath(value).parents
        if parent != PurePosixPath(".")
    }
    if directories != expected_directories:
        raise WorkspaceError(f"{label} contains a stray or empty directory")
    return files


def full_solution_tree(root: Path, declared_files: Iterable[str]) -> SolutionTree:
    """Load a full solution whose files exactly match the workspace contract."""
    declared = _declared(declared_files)
    result = _read_tree(root, "full solution")
    if set(result) != declared:
        raise WorkspaceError("full solution must contain exactly the declared files")
    return result


def overlay_solution_tree(
    provider: Mapping[str, SolutionFile],
    overlay: Path,
    provider_files: Iterable[str],
    consumer_files: Iterable[str],
) -> SolutionTree:
    """Apply one validated sparse solution overlay to a provider solution tree."""
    inherited = _declared(provider_files)
    declared = _declared(consumer_files)
    if set(provider) != inherited:
        raise WorkspaceError("provider solution does not match its declared files")
    if not inherited <= declared:
        raise WorkspaceError("inherited solution cannot remove or rename provider files")
    replacements = _read_tree(overlay, "solution overlay")
    if not replacements:
        raise WorkspaceError("solution overlay must not be empty")
    if not set(replacements) <= declared:
        raise WorkspaceError("solution overlay contains undeclared files")
    new_files = declared - inherited
    if not new_files <= set(replacements):
        raise WorkspaceError("solution overlay must include every newly declared file")
    for relative in set(replacements) & inherited:
        if replacements[relative].data == provider[relative].data:
            raise WorkspaceError(
                f"solution overlay repeats byte-identical inherited file: {relative}"
            )
    result = dict(provider)
    result.update(replacements)
    if set(result) != declared:
        raise WorkspaceError("composed solution does not match declared files")
    return result


def import_solution_files(
    base: Mapping[str, SolutionFile],
    imports: Iterable[SolutionImport],
    declared_files: Iterable[str],
) -> SolutionTree:
    """Add explicitly selected files from composed provider solutions."""
    declared = _declared(declared_files)
    result = dict(base)
    if not set(result) <= declared:
        raise WorkspaceError("import base contains undeclared files")

    for provider_id, provider, paths in imports:
        seen_sources: set[str] = set()
        for source_value, destination_value in paths:
            source = _relative(source_value).as_posix()
            destination = _relative(destination_value).as_posix()
            if source in seen_sources:
                raise WorkspaceError(
                    f"import from {provider_id!r} repeats source file: {source}"
                )
            seen_sources.add(source)
            if source not in provider:
                raise WorkspaceError(
                    f"import from {provider_id!r} has no source file: {source}"
                )
            if destination not in declared:
                raise WorkspaceError(
                    f"import destination is not declared by the consumer: {destination}"
                )
            if destination in result:
                raise WorkspaceError(
                    f"import destination collides with another file: {destination}"
                )
            result[destination] = provider[source]

    if set(result) != declared:
        raise WorkspaceError(
            "base and imports do not cover every declared consumer file"
        )
    return result


def materialize_solution_tree(
    tree: Mapping[str, SolutionFile],
    destination_root: Path,
    stable_id: str,
) -> Path:
    """Atomically materialize a validated composed solution under destination_root."""
    stable_id = _safe_name(stable_id)
    destination_root = _real_directory(Path(destination_root), "solution destination root")
    destination = destination_root / stable_id
    if destination.exists() or destination.is_symlink():
        raise WorkspaceError(f"solution destination already exists: {destination}")
    if not tree:
        raise WorkspaceError("cannot materialize an empty solution")
    temporary = Path(tempfile.mkdtemp(prefix=f".{stable_id}.solution.", dir=destination_root))
    try:
        for value, source in sorted(tree.items()):
            relative = _relative(value)
            target = temporary.joinpath(*relative.parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(source.data)
            target.chmod(source.mode)
        os.replace(temporary, destination)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    return destination
