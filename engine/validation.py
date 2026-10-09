"""Repository validation for curriculum assets, sources, tests, and builds."""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import tokenize
from typing import Iterable, Sequence

from .catalog import Catalog, CatalogError
from .console import Console
from .solutions import full_solution_tree, materialize_solution_tree, overlay_solution_tree
from .workspace import WorkspaceError


class ValidationError(RuntimeError):
    """A repository validation step failed."""


_BUILD_ENVIRONMENT = {
    "LC_ALL": "C",
    "LANG": "C",
    "TZ": "UTC",
    "ARCH": "x86_64",
    "CC": "gcc",
    "HOSTCC": "gcc",
}
_UNTRUSTED_BUILD_VARIABLES = {
    "CROSS_COMPILE",
    "KCONFIG_CONFIG",
    "LOCALVERSION",
    "KCFLAGS",
    "KCPPFLAGS",
    "KAFLAGS",
    "HOSTCFLAGS",
    "HOSTLDFLAGS",
    "LLVM",
    "LLVM_IAS",
    "MAKEFLAGS",
    "MFLAGS",
}
_SUPPORTED_CHECK_INTERPRETERS = {
    "#!/bin/sh": "sh",
    "#!/usr/bin/bash": "bash",
}


def _environment() -> dict[str, str]:
    result = dict(os.environ)
    result.update(_BUILD_ENVIRONMENT)
    for name in _UNTRUSTED_BUILD_VARIABLES:
        result.pop(name, None)
    return result


@dataclass(frozen=True)
class CommandResult:
    returncode: int
    output: str


class Validator:
    """Run every validation stage against one repository root."""

    def __init__(self, root: Path, console: Console) -> None:
        self.root = root.resolve()
        self.console = console
        self.environment = _environment()

    def command(
        self,
        arguments: Sequence[str | os.PathLike[str]],
        *,
        capture: bool = False,
    ) -> CommandResult:
        values = [os.fspath(value) for value in arguments]
        try:
            result = subprocess.run(
                values,
                cwd=self.root,
                env=self.environment,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE if capture else None,
                stderr=subprocess.STDOUT if capture else None,
                text=True,
                check=False,
            )
        except OSError as error:
            raise ValidationError(f"cannot execute {values[0]}: {error}") from error
        return CommandResult(result.returncode, result.stdout or "")

    def require_command(
        self,
        arguments: Sequence[str | os.PathLike[str]],
        message: str,
        *,
        capture: bool = False,
    ) -> CommandResult:
        result = self.command(arguments, capture=capture)
        if result.returncode:
            if result.output:
                self.console.raw(result.output, error=True)
            raise ValidationError(message)
        return result

    def validate(self, *, quick: bool) -> None:
        catalog = Catalog(self.root)
        self.console.success(f"Catalog: {len(catalog.exercises)} exercises")
        self.validate_catalog_assets(catalog)
        self.console.success(
            f"Catalog assets: {len(catalog.exercises)} manifest-owned bundles"
        )
        self.validate_sources()
        self.console.success("Scripts and documentation")
        self.run_unit_tests()
        if not quick:
            self.build_assets(catalog)
        self.console.success("Validation passed")

    def validate_catalog_assets(self, catalog: Catalog) -> None:
        failures: list[str] = []
        catalog_roots = tuple(Path(path) for path in catalog.exercise_roots)
        exercise_roots = tuple(self.root / path for path in catalog_roots)
        manifests = {
            manifest
            for exercise_root in exercise_roots
            for manifest in exercise_root.glob("*/exercise.toml")
        }
        bundle_directories: set[Path] = set()
        for exercise_root in exercise_roots:
            for path in exercise_root.iterdir():
                if path.is_symlink() or not path.is_dir():
                    failures.append(
                        "curriculum entry is not a real bundle directory: "
                        f"{path.relative_to(self.root)}"
                    )
                else:
                    bundle_directories.add(path)

        manifest_directories = {path.parent for path in manifests}
        for path in sorted(bundle_directories - manifest_directories):
            failures.append(f"bundle has no exercise manifest: {path.relative_to(self.root)}")
        declared_manifests = {
            self.root / item.path / "exercise.toml" for item in catalog.exercises
        }
        for path in sorted(manifests - declared_manifests):
            failures.append(f"orphaned exercise manifest: {path.relative_to(self.root)}")
        for path in sorted(declared_manifests - manifests):
            failures.append(f"missing required exercise manifest: {path.relative_to(self.root)}")

        def regular(path: Path, mode: int, label: str) -> None:
            if path.is_symlink() or not path.is_file():
                failures.append(
                    f"{label} is not a regular file: {path.relative_to(self.root)}"
                )
                return
            actual = stat.S_IMODE(path.stat().st_mode)
            if actual != mode:
                failures.append(
                    f"{label} has mode {actual:04o}, expected {mode:04o}: "
                    f"{path.relative_to(self.root)}"
                )

        solution_cache = {}
        for item in catalog.exercises:
            bundle = self.root / item.path
            expected_name = f"{item.serial}-{item.id}"
            if (
                item.path.parent not in catalog_roots
                or item.path.name != expected_name
            ):
                expected_bundle = self.root / catalog_roots[0] / expected_name
                failures.append(
                    f"{item.id}: bundle path must be "
                    f"{expected_bundle.relative_to(self.root)}"
                )
                continue
            if bundle.is_symlink() or not bundle.is_dir():
                failures.append(f"{item.id}: bundle must be a real directory")
                continue

            expected_paths = {
                "exercise.toml",
                "README.md",
                "hint",
                "checks",
                "solution",
            }
            starter_declared = item.workspace.source or item.workspace.additions
            if starter_declared:
                expected_paths.add("starter")
                if Path(starter_declared) != item.path / "starter":
                    failures.append(f"{item.id}: starter must be owned by its bundle")
            if (bundle / "fixtures").exists():
                expected_paths.add("fixtures")
            actual_paths = {path.name for path in bundle.iterdir()}
            if actual_paths != expected_paths:
                failures.append(
                    f"{item.id}: bundle entries mismatch "
                    f"(missing={sorted(expected_paths - actual_paths)}, "
                    f"unknown={sorted(actual_paths - expected_paths)})"
                )

            resources = item.resources
            if Path(resources.readme) != item.path / "README.md":
                failures.append(f"{item.id}: README must be owned by its bundle")
            if Path(resources.hint) != item.path / "hint":
                failures.append(f"{item.id}: hint must be owned by its bundle")
            if Path(resources.solution) != item.path / "solution":
                failures.append(f"{item.id}: solution must be owned by its bundle")
            regular(bundle / "exercise.toml", 0o644, f"{item.id} manifest")
            regular(bundle / "README.md", 0o644, f"{item.id} README")
            regular(bundle / "hint", 0o644, f"{item.id} hint")

            for directory_name in ("checks", "solution"):
                directory = bundle / directory_name
                if directory.is_symlink() or not directory.is_dir():
                    failures.append(
                        f"{item.id}: {directory_name} must be a real directory"
                    )
            for directory_name in ("starter", "fixtures"):
                directory = bundle / directory_name
                if directory.exists() and (
                    directory.is_symlink() or not directory.is_dir()
                ):
                    failures.append(
                        f"{item.id}: {directory_name} must be a real directory"
                    )

            checks = bundle / "checks"
            owned_checks = [] if not checks.is_dir() else sorted(checks.rglob("*"))
            if not any(path.is_file() for path in owned_checks):
                failures.append(f"{item.id}: checks directory is empty")
            for path in owned_checks:
                if path.is_dir() and not path.is_symlink():
                    continue
                regular(path, 0o755, f"{item.id} check")

            for path in sorted(bundle.rglob("*")):
                if path.is_symlink():
                    failures.append(
                        f"{item.id}: bundle contains symlink: {path.relative_to(self.root)}"
                    )
                elif not path.is_dir() and not path.is_file():
                    failures.append(
                        f"{item.id}: bundle contains special file: "
                        f"{path.relative_to(self.root)}"
                    )
                elif path.is_file() and checks not in path.parents:
                    regular(path, 0o644, f"{item.id} asset")

            referenced_checks = [resources.check, resources.host_check]
            referenced_checks.extend(
                case.check for case in catalog.verification_cases(item)
            )
            for value in filter(None, referenced_checks):
                path = self.root / value
                relative = None
                for exercise_root in exercise_roots:
                    try:
                        relative = path.relative_to(exercise_root)
                        break
                    except ValueError:
                        continue
                if relative is None:
                    failures.append(
                        f"{item.id}: check is outside curriculum: {value}"
                    )
                    continue
                if len(relative.parts) < 3 or relative.parts[1] != "checks":
                    failures.append(
                        f"{item.id}: check is outside a bundle checks directory: {value}"
                    )
                regular(path, 0o755, f"{item.id} referenced check")

            try:
                solution = catalog.solution_tree(item, solution_cache)
                if set(solution) != set(item.workspace.files):
                    failures.append(
                        f"{item.id}: composed solution paths differ from workspace files"
                    )
                if any(value.mode != 0o644 for value in solution.values()):
                    failures.append(
                        f"{item.id}: composed solution contains executable source"
                    )

                if item.workspace.imports:
                    starter = catalog.starter_tree(item, solution_cache)
                elif item.workspace.baseline == "starter":
                    starter = full_solution_tree(
                        self.root / item.workspace.source,
                        item.workspace.files,
                    )
                elif item.workspace.baseline == "scaffold":
                    starter = catalog.starter_tree(item, solution_cache)
                else:
                    provider = catalog.baseline_provider(item)
                    if provider is None:
                        raise ValidationError("derived starter has no baseline provider")
                    starter = catalog.solution_tree(provider, solution_cache)
                    if item.workspace.additions:
                        starter = overlay_solution_tree(
                            starter,
                            self.root / item.workspace.additions,
                            provider.workspace.files,
                            item.workspace.files,
                        )
                if set(starter) != set(item.workspace.files):
                    failures.append(
                        f"{item.id}: composed starter paths differ from workspace files"
                    )
                if any(value.mode != 0o644 for value in starter.values()):
                    failures.append(
                        f"{item.id}: composed starter contains executable source"
                    )
            except Exception as error:
                failures.append(f"{item.id}: tree composition failed: {error}")

        tracked = self.require_command(
            ["git", "ls-files", "--", ".rootkitlings"],
            "could not inspect tracked runtime files",
            capture=True,
        ).output.splitlines()
        for relative in tracked:
            if (self.root / relative).is_file():
                failures.append(
                    f"shipped file remains under .rootkitlings: {relative}"
                )
        if failures:
            raise ValidationError("catalog assets:\n  " + "\n  ".join(failures))

    def _source_files(self) -> tuple[Path, ...]:
        candidates = [self.root / "rootkitlings"]
        candidates.extend(sorted((self.root / "engine").glob("*.py")))
        candidates.extend(
            path
            for path in sorted((self.root / "scripts").rglob("*"))
            if path.is_file() and "__pycache__" not in path.parts
        )
        candidates.extend(
            self.root / relative
            for relative in (
                "vm/boot",
                "vm/init",
                "vm/checks/containment",
                "vm/checks/lib.sh",
            )
        )
        return tuple(dict.fromkeys(candidates))

    @staticmethod
    def _first_line(path: Path) -> str:
        try:
            with path.open("r", encoding="utf-8") as source:
                return source.readline().rstrip("\n")
        except (OSError, UnicodeError) as error:
            raise ValidationError(f"cannot read source file {path}: {error}") from error

    def _compile_python(self, path: Path) -> None:
        try:
            with tokenize.open(path) as source:
                text = source.read()
            compile(text, os.fspath(path), "exec")
        except (OSError, SyntaxError, UnicodeError) as error:
            raise ValidationError(f"Python syntax failed for {path}: {error}") from error

    def validate_sources(self) -> None:
        shell_sources: list[tuple[Path, str]] = []
        python_sources: list[Path] = []
        for path in self._source_files():
            interpreter = self._first_line(path)
            if path.suffix == ".py" or "python3" in interpreter:
                python_sources.append(path)
            elif interpreter in _SUPPORTED_CHECK_INTERPRETERS:
                shell_sources.append((path, _SUPPORTED_CHECK_INTERPRETERS[interpreter]))

        for path in python_sources:
            self._compile_python(path)
        for path, shell in shell_sources:
            self.require_command(
                [shell, "-n", path],
                f"shell syntax failed for {path.relative_to(self.root)}",
                capture=True,
            )

        check_sources: list[tuple[Path, str]] = []
        catalog = Catalog(self.root)
        check_paths = sorted(
            path
            for item in catalog.exercises
            for path in (self.root / item.path / "checks").rglob("*")
        )
        for path in check_paths:
            if not path.is_file():
                continue
            interpreter = self._first_line(path)
            shell = _SUPPORTED_CHECK_INTERPRETERS.get(interpreter)
            if shell is None:
                raise ValidationError(
                    "bundle check has unsupported interpreter: "
                    f"{path.relative_to(self.root)}"
                )
            check_sources.append((path, shell))
            self.require_command(
                [shell, "-n", path],
                f"shell syntax failed for {path.relative_to(self.root)}",
                capture=True,
            )

        self.require_command(
            [sys.executable, "scripts/format-docs", "--check"],
            "documentation formatting validation failed",
        )
        self.require_command(
            [sys.executable, "scripts/check-doc-links"],
            "documentation link validation failed",
        )

        shellcheck = shutil.which("shellcheck")
        if shellcheck is None:
            return
        for path, shell in shell_sources:
            dialect = (
                "busybox"
                if path == self.root / "vm/init"
                or self.root / "vm/checks" in path.parents
                else shell
            )
            self.require_command(
                [shellcheck, "-S", "warning", "-s", dialect, path],
                f"shellcheck failed for {path.relative_to(self.root)}",
                capture=True,
            )
        for path, shell in check_sources:
            self.require_command(
                [shellcheck, "-S", "warning", "-x", "-s", shell, path],
                f"shellcheck failed for {path.relative_to(self.root)}",
                capture=True,
            )

    def run_unit_tests(self) -> None:
        self.console.progress("Running unit tests")
        self.require_command(
            [sys.executable, "-m", "unittest", "discover", "-s", "tests"],
            "unit tests failed",
        )

    def build_assets(self, catalog: Catalog) -> None:
        located = self.require_command(
            [self.root / "scripts" / "kernel", "build-dir"],
            "course kernel is not ready; run scripts/kernel ensure",
            capture=True,
        )
        kernel_build = Path(located.output.strip())
        if kernel_build.is_symlink() or not kernel_build.is_dir():
            raise ValidationError(
                f"kernel build directory not found: {kernel_build}"
            )

        starters = 0
        derived = 0
        solutions = 0
        with tempfile.TemporaryDirectory(
            prefix="rootkitlings-validate-"
        ) as temporary:
            temporary_root = Path(temporary)
            starter_root = temporary_root / "starters"
            solution_root = temporary_root / "solutions"
            starter_root.mkdir()
            solution_root.mkdir()

            for item in catalog.exercises:
                if item.workspace.baseline == "starter":
                    starter = (
                        catalog.starter_tree(item)
                        if item.workspace.imports
                        else full_solution_tree(
                            catalog.root / item.workspace.source,
                            item.workspace.files,
                        )
                    )
                    materialize_solution_tree(starter, starter_root, item.id)
                catalog.materialize_solution(item, solution_root, item.id)

            for item in catalog.display_order():
                if item.workspace.baseline == "starter":
                    self._build_module(
                        kernel_build,
                        starter_root / item.id,
                        item.workspace.build,
                        f"starter does not build warning-free: {item.id}",
                    )
                    starters += 1
                else:
                    derived += 1
                self._build_module(
                    kernel_build,
                    solution_root / item.id,
                    item.workspace.build,
                    f"solution does not build warning-free: {item.id}",
                )
                solutions += 1

        self.console.success(
            f"Starters: {starters} warning-free builds; "
            f"{derived} derived starters structurally validated"
        )
        self.console.success(f"Solutions: {solutions} warning-free builds")

    def _build_module(
        self,
        kernel_build: Path,
        tree: Path,
        relative_build: str,
        failure: str,
    ) -> None:
        build_path = tree if relative_build == "." else tree / relative_build
        result = self.command(
            [
                "make",
                "-s",
                "-C",
                kernel_build,
                f"M={build_path}",
                "W=1",
                "modules",
            ],
            capture=True,
        )
        if result.returncode or "warning:" in result.output:
            if result.output:
                self.console.raw(result.output, error=True)
            raise ValidationError(failure)


def _usage(program: str) -> str:
    return f"usage: {program} [--quick]"


def main(arguments: Iterable[str], *, root: Path) -> int:
    values = list(arguments)
    console = Console("rootkitlings.validation")
    if values not in ([], ["--quick"]):
        console.raw(_usage("scripts/validate") + "\n", error=True)
        return 2
    try:
        Validator(root, console).validate(quick=bool(values))
    except (CatalogError, ValidationError, WorkspaceError, OSError) as error:
        console.error(str(error))
        return 1
    return 0
