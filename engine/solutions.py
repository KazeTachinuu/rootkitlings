"""Validate and materialize reference solution trees.

Each exercise is standalone: its solution directory contains every declared
file. No overlays, no inheritance, no imports.
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


def materialize_solution_tree(
    tree: Mapping[str, SolutionFile],
    destination_root: Path,
    stable_id: str,
) -> Path:
    """Atomically materialize a validated solution under destination_root."""
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
