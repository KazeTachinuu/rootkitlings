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
from .solutions import full_solution_tree, materialize_solution_tree
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
        self.run_unit_tests(skip_vm=quick)
        if not quick:
            self.build_assets(catalog)
            self.run_checker_tests()
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
        non_bundle = bundle_directories - manifest_directories
        bundle_directories -= non_bundle
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
                "solution",
            }
            has_local_checks = (bundle / "checks").is_dir()
            if has_local_checks:
                expected_paths.add("checks")
            if item.workspace.source:
                expected_paths.add("starter")
                if Path(item.workspace.source) != item.path / "starter":
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

            solution_dir = bundle / "solution"
            if solution_dir.is_symlink() or not solution_dir.is_dir():
                failures.append(f"{item.id}: solution must be a real directory")
            for directory_name in ("starter", "fixtures"):
                directory = bundle / directory_name
                if directory.exists() and (
                    directory.is_symlink() or not directory.is_dir()
                ):
                    failures.append(
                        f"{item.id}: {directory_name} must be a real directory"
                    )

            checks = bundle / "checks"
            if has_local_checks:
                owned_checks = sorted(checks.rglob("*"))
                if not any(path.is_file() for path in owned_checks):
                    failures.append(f"{item.id}: checks directory is empty")
                regular(checks / "test", 0o755, f"{item.id} checker test")
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

            referenced_checks = [resources.host_check]
            referenced_checks.extend(
                case.check for case in item.verification.cases
            )
            for value in filter(None, referenced_checks):
                path = self.root / value
                if not path.is_file():
                    failures.append(
                        f"{item.id}: referenced check does not exist: {value}"
                    )
                    continue
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
                starter = full_solution_tree(
                    self.root / item.workspace.source,
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
            ["git", "ls-files", "--", ".rklings", "work"],
            "could not inspect tracked runtime files",
            capture=True,
        ).output.splitlines()
        for relative in tracked:
            if (self.root / relative).is_file():
                failures.append(
                    f"shipped learner runtime file remains: {relative}"
                )
        if failures:
            raise ValidationError("catalog assets:\n  " + "\n  ".join(failures))

    def _source_files(self) -> tuple[Path, ...]:
        candidates = [self.root / "rklings"]
        candidates.extend(sorted((self.root / "engine").glob("*.py")))
        candidates.extend(
            path
            for path in sorted((self.root / "scripts").rglob("*"))
            if path.is_file() and "__pycache__" not in path.parts
        )
        for vm_path in ("vm/boot", "vm/init"):
            candidates.append(self.root / vm_path)
        candidates.extend(
            sorted(
                path
                for path in (self.root / "vm/checks").iterdir()
                if path.is_file()
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
        self.console.progress("Checking source syntax")
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
        self.console.success(
            f"Syntax: {len(python_sources)} Python, "
            f"{len(shell_sources) + len(check_sources)} shell"
        )

        self.console.progress("Checking documentation")
        self.require_command(
            [sys.executable, "scripts/format-docs", "--check"],
            "documentation formatting validation failed",
            capture=True,
        )
        self.require_command(
            [sys.executable, "scripts/check-doc-links"],
            "documentation link validation failed",
            capture=True,
        )

        shellcheck = shutil.which("shellcheck")
        if shellcheck is None:
            return
        self.console.progress("Running shellcheck")
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

    def run_unit_tests(self, *, skip_vm: bool = False) -> None:
        self.console.progress("Running unit tests")
        test_dir = self.root / "tests"
        if skip_vm:
            modules = [
                f.stem for f in sorted(test_dir.glob("test_*.py"))
                if not f.stem.startswith("test_vm")
            ]
        else:
            modules = [
                f.stem for f in sorted(test_dir.glob("test_*.py"))
            ]
        total = len(modules)
        failed: list[str] = []
        for index, module in enumerate(modules, 1):
            label = module.removeprefix("test_")
            self.console.progress(f"  {label} ({index}/{total})")
            result = self.command(
                [sys.executable, "-m", "unittest", f"tests.{module}"],
                capture=True,
            )
            if result.returncode:
                failed.append(module)
                self.console.raw(result.output, error=True)
        if failed:
            raise ValidationError(
                f"unit tests failed: {', '.join(failed)}"
            )
        self.console.success(f"Unit tests: {total} modules")

    def run_checker_tests(self) -> None:
        self.console.progress("Running checker regression tests")
        self.require_command(
            [self.root / "scripts" / "test-checkers"],
            "checker regression tests failed",
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
            prefix="rklings-validate-"
        ) as temporary:
            temporary_root = Path(temporary)
            starter_root = temporary_root / "starters"
            solution_root = temporary_root / "solutions"
            starter_root.mkdir()
            solution_root.mkdir()

            shared_include = catalog.root / "curriculum" / "include"
            if shared_include.is_dir():
                (temporary_root / "include").symlink_to(shared_include)

            self.console.progress("Materializing starters and solutions")
            for item in catalog.exercises:
                starter = full_solution_tree(
                    catalog.root / item.workspace.source,
                    item.workspace.files,
                )
                materialize_solution_tree(starter, starter_root, item.id)
                catalog.materialize_solution(item, solution_root, item.id)

            required = list(catalog.required())
            total = len(required)
            for index, item in enumerate(required, 1):
                self.console.progress(
                    f"Building {item.id} ({index}/{total})"
                )
                self._build_module(
                    kernel_build,
                    starter_root / item.id,
                    item.workspace.build,
                    f"starter does not compile: {item.id}",
                    warn_free=False,
                )
                starters += 1
                self._build_module(
                    kernel_build,
                    solution_root / item.id,
                    item.workspace.build,
                    f"solution does not build warning-free: {item.id}",
                )
                solutions += 1

        self.console.success(f"Starters: {starters} builds")
        self.console.success(f"Solutions: {solutions} warning-free builds")

    def _build_module(
        self,
        kernel_build: Path,
        tree: Path,
        relative_build: str,
        failure: str,
        *,
        warn_free: bool = True,
    ) -> None:
        build_path = tree if relative_build == "." else tree / relative_build
        command = [
            "make", "-s", "-C", kernel_build,
            f"M={build_path}", "RKLINGS_FAIL_POINT=0", "modules",
        ]
        if warn_free:
            command.insert(-1, "W=1")
        result = self.command(command, capture=True)
        if result.returncode or (warn_free and "warning:" in result.output):
            if result.output:
                self.console.raw(result.output, error=True)
            raise ValidationError(failure)


def _usage(program: str) -> str:
    return f"usage: {program} [--quick]"


def main(arguments: Iterable[str], *, root: Path) -> int:
    values = list(arguments)
    console = Console("rklings.validation")
    if values not in ([], ["--quick"]):
        console.raw(_usage("scripts/validate") + "\n", error=True)
        return 2
    try:
        Validator(root, console).validate(quick=bool(values))
    except (CatalogError, ValidationError, WorkspaceError, OSError) as error:
        console.error(str(error))
        return 1
    return 0
