from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import io
from pathlib import Path
import runpy
import subprocess
import unittest
from unittest import mock

from engine.catalog import Catalog
from engine.process import ProcessTimeout


ROOT = Path(__file__).resolve().parent.parent
RUNNER = runpy.run_path(str(ROOT / "scripts/test-checkers"))


class CheckerRunnerTests(unittest.TestCase):
    def test_selectors_run_catalog_checkers_in_course_order(self) -> None:
        catalog = Catalog(ROOT)
        selectors = ("L01", "L00")
        selected = {catalog.resolve(value).id for value in selectors}
        expected = [
            ROOT / item.path / "checks/test"
            for item in catalog.required()
            if item.id in selected
        ]
        run = mock.Mock(
            side_effect=(
                subprocess.CompletedProcess((), 0),
                subprocess.CompletedProcess((), 1),
            )
        )
        with mock.patch.dict(RUNNER["main"].__globals__, {"run_process": run}):
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                status = RUNNER["main"](list(selectors))

        self.assertEqual(status, 1)
        paths = [Path(call.args[0][0]) for call in run.call_args_list]
        self.assertEqual(paths, expected)
        for call in run.call_args_list:
            self.assertEqual(
                call.kwargs,
                {
                    "cwd": ROOT,
                    "timeout": RUNNER["CHECKER_TIMEOUT_SECONDS"],
                    "discard_stdin": True,
                    "capture": True,
                },
            )

    def test_timeout_fails_the_checker_without_hanging_the_runner(self) -> None:
        run = mock.Mock(
            side_effect=ProcessTimeout(
                ("checker",),
                RUNNER["CHECKER_TIMEOUT_SECONDS"],
                "partial output",
            )
        )
        stderr = io.StringIO()
        with mock.patch.dict(RUNNER["main"].__globals__, {"run_process": run}):
            with redirect_stdout(io.StringIO()), redirect_stderr(stderr):
                status = RUNNER["main"](["L00"])
        self.assertEqual(status, 1)
        self.assertIn("timed out", stderr.getvalue())

    def test_invalid_selector_is_a_usage_error(self) -> None:
        run = mock.Mock()
        with mock.patch.dict(RUNNER["main"].__globals__, {"run_process": run}):
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                status = RUNNER["main"](["not-an-exercise"])
        self.assertEqual(status, 2)
        run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
