"""Integration tests: build solutions and run check scripts inside a VM.

Auto-discovers exercises from the course catalog. Each verification
case becomes a test method, built with its own variables.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from engine.catalog import Catalog  # noqa: E402
from tests.vm import VM, build_module, clean_module, REPO, VMError  # noqa: E402


class _LaxCatalog(Catalog):
    def validate(self):
        pass


CATALOG = _LaxCatalog(REPO)
_SOLUTION_CACHE: dict[str, Path] = {}
_BUILD_ROOT = Path(tempfile.mkdtemp(prefix="rklings-test-"))


def _get_solution(item) -> Path:
    """Materialize and cache an exercise's solution source tree."""
    if item.id in _SOLUTION_CACHE:
        return _SOLUTION_CACHE[item.id]
    src = CATALOG.materialize_solution(item, _BUILD_ROOT)
    _SOLUTION_CACHE[item.id] = src
    return src


class VMSolutionTest(unittest.TestCase):
    """Base for auto-generated exercise test classes."""
    pass


def _make_check_test(item, case, tool_binaries: list[str], timeout: int,
                     bare: bool = False):
    check = REPO / case.check
    check_args = case.check_arguments

    def test(self):
        src = _get_solution(item)
        clean_module(src)
        build_vars = dict(case.build_variables)
        ko = build_module(src, variables=build_vars)
        vm = VM()
        result = vm.run_check(check, ko, timeout=timeout,
                              check_args=check_args,
                              tools=tuple(tool_binaries),
                              bare=bare)
        self.assertEqual(result.returncode, 0, result.output)

    test.__qualname__ = f"{item.id}::{case.id}"
    return test


def _make_build_fail_test(item, case):
    def test(self):
        src = _get_solution(item)
        clean_module(src)
        build_vars = dict(case.build_variables)
        with self.assertRaises((subprocess.CalledProcessError, VMError)):
            build_module(src, variables=build_vars)

    test.__qualname__ = f"{item.id}::{case.id}"
    return test


def _register_exercise(item) -> type | None:
    cases = item.verification.cases
    if not cases:
        return None

    timeout = item.verification.timeout_seconds
    tool_registry = {tool.id: tool.binary for tool in CATALOG.tools}
    tool_binaries = [tool_registry[tid] for tid in item.verification.tools]
    bare = item.stage == "module-core"

    attrs: dict = {}
    for case in cases:
        method_name = f"test_{case.id.replace('-', '_')}"

        if case.expect_build == "fail":
            attrs[method_name] = _make_build_fail_test(item, case)
        elif case.check:
            attrs[method_name] = _make_check_test(
                item, case, tool_binaries, timeout, bare=bare,
            )

    if not attrs:
        return None

    exercise_dir = REPO / item.path
    cls_name = exercise_dir.name.replace("-", "_") + "_Tests"
    return type(cls_name, (VMSolutionTest,), attrs)


for _item in CATALOG.exercises:
    try:
        _cls = _register_exercise(_item)
    except Exception:
        continue
    if _cls:
        globals()[_cls.__name__] = _cls


if __name__ == "__main__":
    unittest.main()
