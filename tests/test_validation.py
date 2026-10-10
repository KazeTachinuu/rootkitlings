from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import io
import os
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest import mock

from engine.console import Console
from engine.validation import CommandResult, ValidationError, Validator, _environment, main


class ValidationTests(unittest.TestCase):
    def test_build_environment_is_deterministic(self) -> None:
        with mock.patch.dict(
            os.environ,
            {"ARCH": "arm64", "KCFLAGS": "untrusted", "MAKEFLAGS": "-j99"},
        ):
            environment = _environment()
        self.assertEqual(environment["ARCH"], "x86_64")
        self.assertEqual(environment["LC_ALL"], "C")
        self.assertNotIn("KCFLAGS", environment)
        self.assertNotIn("MAKEFLAGS", environment)

    def test_invalid_arguments_return_usage_error(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()
        with redirect_stdout(stdout), redirect_stderr(stderr):
            result = main(["--unknown"], root=Path.cwd())
        self.assertEqual(result, 2)
        self.assertEqual(stdout.getvalue(), "")
        self.assertEqual(stderr.getvalue(), "usage: scripts/validate [--quick]\n")

    def test_warning_makes_module_build_fail(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            validator = Validator(Path(temporary), Console("validation-test"))
            validator.command = mock.Mock(
                return_value=CommandResult(0, "source.c: warning: unsafe\n")
            )
            with redirect_stderr(io.StringIO()):
                with self.assertRaisesRegex(ValidationError, "warning-free"):
                    validator._build_module(
                        Path(temporary) / "kernel",
                        Path(temporary) / "source",
                        ".",
                        "solution does not build warning-free",
                    )

    def test_quick_validation_skips_kernel_builds(self) -> None:
        validator = Validator(Path.cwd(), Console("validation-test-quick"))
        catalog = SimpleNamespace(exercises=())
        validator.validate_catalog_assets = mock.Mock()
        validator.validate_sources = mock.Mock()
        validator.run_unit_tests = mock.Mock()
        validator.build_assets = mock.Mock()
        validator.run_checker_tests = mock.Mock()
        with mock.patch("engine.validation.Catalog", return_value=catalog):
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                validator.validate(quick=True)
        validator.build_assets.assert_not_called()
        validator.run_checker_tests.assert_not_called()


if __name__ == "__main__":
    unittest.main()
