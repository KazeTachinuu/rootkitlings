from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parent.parent


class VMBootTests(unittest.TestCase):
    def test_runtime_options_require_runtime_setup(self) -> None:
        result = subprocess.run(
            [ROOT / "vm/boot", "--control-port", "40001"],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env={**os.environ, "NO_COLOR": "1"},
        )
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("runtime options require --runtime-setup", result.stdout)

    def test_runtime_setup_rejects_an_invalid_control_key(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            setup = path / "runtime-setup"
            module = path / "module.ko"
            key = path / "control.key"
            setup.write_text("#!/bin/sh\n", encoding="utf-8")
            module.write_bytes(b"module")
            key.write_text("bad\n", encoding="ascii")
            result = subprocess.run(
                [
                    ROOT / "vm/boot",
                    "--runtime-setup", setup,
                    "--control-key-file", key,
                    "--control-port", "40001",
                    "--shell-port", "40002",
                    module,
                ],
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                env={**os.environ, "NO_COLOR": "1"},
            )
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("64 hexadecimal digits and a newline", result.stdout)

    def test_gdb_mode_reports_its_optional_dependency_directly(self) -> None:
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            path = Path(directory)
            dirname = shutil.which("dirname")
            self.assertIsNotNone(dirname)
            (path / "dirname").symlink_to(dirname)
            result = subprocess.run(
                [ROOT / "vm/boot", "--gdb"],
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                env={**os.environ, "PATH": str(path), "NO_COLOR": "1"},
            )

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("GDB not found", result.stdout)
        self.assertIn("make doctor", result.stdout)


if __name__ == "__main__":
    unittest.main()
