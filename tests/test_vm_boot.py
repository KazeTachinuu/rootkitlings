from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parent.parent


class VMBootTests(unittest.TestCase):
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
