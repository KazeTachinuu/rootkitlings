"""Generate stable clangd commands for the physical curriculum sources."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shlex
import shutil
import tempfile

from .build_env import module_build_environment
from .catalog import Catalog
from .process import ProcessError, ProcessTimeout, run_process


ROOT = Path(__file__).resolve().parent.parent
DATABASE = ROOT / "compile_commands.json"


class CompileDatabaseError(RuntimeError):
    """The compilation database could not be generated safely."""


def bundle_source_files(root: Path = ROOT) -> set[Path]:
    """Return physical C sources owned by manifest-bearing course bundles."""
    sources: set[Path] = set()
    for manifest in sorted((root / "curriculum").glob("*/exercise.toml")):
        bundle = manifest.parent
        for directory in ("starter", "solution"):
            sources.update((bundle / directory).glob("**/*.c"))
    return sources


def entry_file(entry: dict[str, object]) -> Path:
    try:
        value = Path(str(entry["file"]))
        directory = Path(str(entry["directory"]))
    except KeyError as error:
        raise CompileDatabaseError("compile command lacks file or directory") from error
    return value if value.is_absolute() else directory / value


def base_compile_entry(
    database: list[dict[str, object]],
    reference_sources: set[Path],
) -> dict[str, object]:
    expected = {path.resolve() for path in reference_sources}
    matches = [
        (entry_file(entry).resolve(), entry)
        for entry in database
        if entry_file(entry).resolve() in expected
    ]
    if not matches:
        raise CompileDatabaseError(
            "Bear did not capture a reference-source compile command"
        )
    return min(matches, key=lambda match: os.fspath(match[0]))[1]


def source_argument(arguments: list[str], original: Path) -> int:
    resolved = original.resolve()
    for index, argument in enumerate(arguments):
        if argument.endswith(".c"):
            candidate = Path(argument)
            if not candidate.is_absolute():
                candidate = original.parent.parent / candidate
            if candidate.resolve() == resolved:
                return index
    candidates = [
        index for index, argument in enumerate(arguments) if argument.endswith(".c")
    ]
    if len(candidates) != 1:
        raise CompileDatabaseError("reference command has no unique C source argument")
    return candidates[0]


def source_include_directories(source: Path, root: Path = ROOT) -> tuple[Path, ...]:
    try:
        relative = source.resolve().relative_to((root / "curriculum").resolve())
    except ValueError as error:
        raise CompileDatabaseError(f"source is outside the curriculum: {source}") from error
    if len(relative.parts) < 3 or relative.parts[1] not in {"starter", "solution"}:
        raise CompileDatabaseError(f"source is not in a starter or solution tree: {source}")
    bundle = root / "curriculum" / relative.parts[0]
    own_tree = relative.parts[1]
    other_tree = "solution" if own_tree == "starter" else "starter"
    candidates = (bundle / own_tree / "include", bundle / other_tree / "include")
    return tuple(path.resolve() for path in candidates if path.is_dir())


def source_defines(source: Path, root: Path = ROOT) -> tuple[str, ...]:
    """Derive course-specific preprocessor defaults from the owning Kbuilds."""
    relative = source.resolve().relative_to((root / "curriculum").resolve())
    bundle = root / "curriculum" / relative.parts[0]
    own_tree = relative.parts[1]
    other_tree = "solution" if own_tree == "starter" else "starter"
    kbuilds = (bundle / own_tree / "Kbuild", bundle / other_tree / "Kbuild")
    for kbuild in kbuilds:
        if kbuild.is_file() and "ROOTKITLINGS_FAIL_POINT" in kbuild.read_text(
            encoding="utf-8"
        ):
            return ("-DROOTKITLINGS_FAIL_POINT=0",)
    return ()


def expanded_entry(
    base: dict[str, object],
    source: Path,
    root: Path = ROOT,
) -> dict[str, object]:
    if "arguments" in base:
        arguments = [str(value) for value in base["arguments"]]
    elif "command" in base:
        arguments = shlex.split(str(base["command"]))
    else:
        raise CompileDatabaseError(
            "reference compile command has neither arguments nor command"
        )
    original = entry_file(base)
    source_index = source_argument(arguments, original)
    cleaned: list[str] = []
    skip_next = False
    for index, argument in enumerate(arguments):
        if skip_next:
            skip_next = False
            continue
        if index == source_index or argument == "-c":
            continue
        if argument == "-o":
            skip_next = True
            continue
        if argument.startswith(("-Wp,-MD,", "-Wp,-MMD,")):
            continue
        if argument.startswith("-DKBUILD_BASENAME="):
            argument = f'-DKBUILD_BASENAME="{source.stem}"'
        cleaned.append(argument)
    existing_includes = {
        argument[2:] for argument in cleaned if argument.startswith("-I")
    }
    for include in source_include_directories(source, root):
        value = os.fspath(include)
        if value not in existing_includes:
            cleaned.append(f"-I{value}")
    existing_defines = {
        argument.split("=", 1)[0] for argument in cleaned if argument.startswith("-D")
    }
    for define in source_defines(source, root):
        if define.split("=", 1)[0] not in existing_defines:
            cleaned.append(define)
    replacement = os.fspath(source.resolve())
    cleaned.extend(("-c", replacement))
    return {
        "directory": os.fspath(root.resolve()),
        "file": replacement,
        "arguments": cleaned,
    }


def captured_database(catalog: Catalog, temporary_root: Path) -> list[dict[str, object]]:
    bear = shutil.which("bear")
    if bear is None:
        raise CompileDatabaseError("Bear is required; install the 'bear' package")
    kernel = run_process(
        [ROOT / "scripts/kernel", "build-dir"],
        cwd=ROOT,
        timeout=10,
        capture=True,
        discard_stdin=True,
    )
    build_directory = Path((kernel.stdout or "").strip())
    if kernel.returncode or build_directory.is_symlink() or not build_directory.is_dir():
        raise CompileDatabaseError(
            "course kernel is not ready; run: ./rootkitlings setup"
        )
    reference = catalog.required()[0]
    workspace = catalog.materialize_solution(reference, temporary_root, "reference")
    raw_database = temporary_root / "captured.json"
    result = run_process(
        [
            bear,
            "--output",
            raw_database,
            "--",
            "make",
            "-s",
            "-C",
            build_directory,
            f"M={workspace}",
            "W=1",
            "modules",
        ],
        cwd=ROOT,
        timeout=180,
        capture=True,
        discard_stdin=True,
        env=module_build_environment(),
    )
    if result.returncode:
        details = (result.stdout or "").strip()
        suffix = f"\n{details}" if details else ""
        raise CompileDatabaseError(f"Bear reference build failed{suffix}")
    try:
        captured = json.loads(raw_database.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CompileDatabaseError(f"cannot read Bear output: {error}") from error
    if not isinstance(captured, list) or not all(
        isinstance(entry, dict) for entry in captured
    ):
        raise CompileDatabaseError("Bear output is not a compile-command array")
    reference_sources = {
        workspace / relative
        for relative in reference.workspace.files
        if relative.endswith(".c")
    }
    base = base_compile_entry(captured, reference_sources)
    return [expanded_entry(base, source) for source in sorted(bundle_source_files())]


def write_database(database: list[dict[str, object]], path: Path = DATABASE) -> None:
    if not database:
        raise CompileDatabaseError("the curriculum contains no physical C sources")
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=".compile_commands.", suffix=".json", dir=path.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(database, stream, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def generate() -> int:
    try:
        with tempfile.TemporaryDirectory(prefix="rootkitlings-compile-db-") as directory:
            database = captured_database(Catalog(ROOT), Path(directory))
        write_database(database)
    except (CompileDatabaseError, OSError, ProcessError, ProcessTimeout) as error:
        raise CompileDatabaseError(str(error)) from error
    return len(database)
