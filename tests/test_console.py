from __future__ import annotations

import io
import os
import sys
import unittest
from unittest import mock

from engine.console import Console


class Stream(io.StringIO):
    def __init__(self, tty: bool) -> None:
        super().__init__()
        self.tty = tty

    def isatty(self) -> bool:
        return self.tty


class ConsoleTests(unittest.TestCase):
    def setUp(self) -> None:
        self.console = Console(f"rootkitlings.test.{id(self)}")

    def test_color_is_decided_for_each_destination_stream(self) -> None:
        stdout = Stream(False)
        stderr = Stream(True)
        with (
            mock.patch.object(sys, "stdout", stdout),
            mock.patch.object(sys, "stderr", stderr),
            mock.patch.dict(os.environ, {"TERM": "xterm"}, clear=False),
        ):
            os.environ.pop("NO_COLOR", None)
            self.console.heading("Heading")
            self.console.error("broken")
        self.assertNotIn("\x1b", stdout.getvalue())
        self.assertIn("\x1b", stderr.getvalue())

    def test_empty_no_color_value_disables_ansi(self) -> None:
        stdout = Stream(True)
        stderr = Stream(True)
        with (
            mock.patch.object(sys, "stdout", stdout),
            mock.patch.object(sys, "stderr", stderr),
            mock.patch.dict(
                os.environ,
                {"TERM": "xterm", "NO_COLOR": ""},
                clear=False,
            ),
        ):
            self.console.success("ready")
            self.console.error("broken")
        self.assertNotIn("\x1b", stdout.getvalue())
        self.assertNotIn("\x1b", stderr.getvalue())

    def test_logs_use_stdout_for_progress_and_stderr_for_errors(self) -> None:
        stdout = Stream(False)
        stderr = Stream(False)
        with (
            mock.patch.object(sys, "stdout", stdout),
            mock.patch.object(sys, "stderr", stderr),
        ):
            self.console.success("ready")
            self.console.error("broken")
        self.assertEqual(stdout.getvalue(), "[+] ready\n")
        self.assertEqual(stderr.getvalue(), "[x] broken\n")

    def test_invalid_columns_falls_back_without_raising(self) -> None:
        with mock.patch.dict(os.environ, {"COLUMNS": "not-a-number"}, clear=False):
            self.assertGreater(self.console.terminal_width(), 0)

    def test_raw_block_terminates_unterminated_command_output(self) -> None:
        stdout = Stream(False)
        with mock.patch.object(sys, "stdout", stdout):
            self.console.raw_block("last line")
            self.console.success("ready")
        self.assertEqual(stdout.getvalue(), "last line\n[+] ready\n")

    def test_transcript_shell_quotes_each_argument(self) -> None:
        stdout = Stream(False)
        with mock.patch.object(sys, "stdout", stdout):
            self.console.transcript(("make", "VALUE=two words", "$literal"))
        self.assertEqual(
            stdout.getvalue(),
            "$ make 'VALUE=two words' '$literal'\n",
        )

    def test_process_output_never_hides_an_empty_result(self) -> None:
        stdout = Stream(False)
        with mock.patch.object(sys, "stdout", stdout):
            self.console.process_output("")
        self.assertEqual(stdout.getvalue(), "(command produced no output)\n")

    def test_process_result_distinguishes_success_rejection_and_no_exit(self) -> None:
        stdout = Stream(False)
        with mock.patch.object(sys, "stdout", stdout):
            self.console.process_result(0)
            self.console.process_result(2, expected_nonzero=True)
            self.console.process_result(None)
        self.assertEqual(
            stdout.getvalue().splitlines(),
            [
                "Exit       0",
                "Exit       2 (expected non-zero)",
                "Exit       not available",
            ],
        )


if __name__ == "__main__":
    unittest.main()
