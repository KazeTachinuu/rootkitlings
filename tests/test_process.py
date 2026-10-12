from pathlib import Path
import os
import shlex
import signal
import tempfile
import time
import unittest

from engine.process import ProcessError, ProcessTimeout, run_process


class ProcessTests(unittest.TestCase):
    def test_explicit_environment_does_not_inherit_ambient_values(self) -> None:
        result = run_process(
            ["/bin/sh", "-c", "printf '%s|%s' \"$LC_ALL\" \"${KCFLAGS-unset}\""],
            capture=True,
            env={"PATH": os.environ.get("PATH", os.defpath), "LC_ALL": "C"},
        )
        self.assertEqual(result.stdout, "C|unset")

    def test_launch_error_is_translated(self) -> None:
        with self.assertRaisesRegex(ProcessError, "cannot execute"):
            run_process(["/definitely/missing/rklings-command"])

    def test_timeout_kills_descendant_process_group(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            pid_file = Path(temporary) / "child.pid"
            with self.assertRaises(ProcessTimeout):
                run_process(
                    [
                        "/bin/sh",
                        "-c",
                        f"sleep 30 & echo $! > {pid_file}; wait",
                    ],
                    timeout=0.1,
                    capture=True,
                )
            child_pid = int(pid_file.read_text())
            for _ in range(20):
                try:
                    os.kill(child_pid, 0)
                except ProcessLookupError:
                    break
                time.sleep(0.05)
            else:
                self.fail("timed-out descendant process remained alive")

    def test_escaped_descendant_cannot_make_timeout_cleanup_hang(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            pid_file = Path(temporary) / "escaped.pid"
            command = (
                "setsid /bin/sh -c "
                + shlex.quote(f"echo $$ > {pid_file}; sleep 30")
                + " &"
            )
            started = time.monotonic()
            try:
                with self.assertRaises(ProcessTimeout):
                    run_process(
                        ["/bin/sh", "-c", command],
                        timeout=0.1,
                        capture=True,
                    )
            finally:
                if pid_file.exists():
                    try:
                        os.kill(int(pid_file.read_text()), signal.SIGKILL)
                    except ProcessLookupError:
                        pass
            self.assertLess(time.monotonic() - started, 4)


if __name__ == "__main__":
    unittest.main()
