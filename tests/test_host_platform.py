from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parent.parent


class HostPlatformTests(unittest.TestCase):
    def test_debian_compiler_runtime_package_mapping(self) -> None:
        command = (
            f'. "{ROOT / "scripts/lib/host.sh"}"; '
            'family=debian; package_name compiler-rt'
        )
        result = subprocess.run(
            ["/usr/bin/bash", "-c", command],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(result.stdout, "libclang-rt-dev")

    def test_fedora_script_package_mapping(self) -> None:
        command = (
            f'. "{ROOT / "scripts/lib/host.sh"}"; '
            'family=fedora; package_name script'
        )
        result = subprocess.run(
            ["/usr/bin/bash", "-c", command],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(result.stdout, "util-linux-script")

    def test_doctor_rejects_non_linux_before_dependency_checks(self) -> None:
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            path = Path(directory)
            dirname = shutil.which("dirname")
            self.assertIsNotNone(dirname)
            (path / "dirname").symlink_to(dirname)
            uname = path / "uname"
            uname.write_text("#!/bin/sh\nprintf 'Darwin\\n'\n", encoding="utf-8")
            uname.chmod(0o755)
            result = subprocess.run(
                ["/usr/bin/bash", ROOT / "scripts/doctor"],
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                env={**os.environ, "PATH": str(path), "NO_COLOR": "1"},
            )

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("Darwin is unsupported", result.stdout)
        self.assertIn("x86-64 Linux", result.stdout)
        self.assertNotIn("qemu-system-x86_64", result.stdout)

    def test_doctor_rejects_wsl1_before_dependency_checks(self) -> None:
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            path = Path(directory)
            dirname = shutil.which("dirname")
            self.assertIsNotNone(dirname)
            (path / "dirname").symlink_to(dirname)
            uname = path / "uname"
            uname.write_text(
                "#!/bin/sh\n"
                "case $1 in\n"
                "  -s) printf 'Linux\\n' ;;\n"
                "  -r) printf '4.4.0-Microsoft\\n' ;;\n"
                "  *) printf 'x86_64\\n' ;;\n"
                "esac\n",
                encoding="utf-8",
            )
            uname.chmod(0o755)
            result = subprocess.run(
                ["/usr/bin/bash", ROOT / "scripts/doctor"],
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                env={
                    **os.environ,
                    "PATH": str(path),
                    "NO_COLOR": "1",
                    "WSL_DISTRO_NAME": "Ubuntu",
                },
            )

        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("WSL1 is unsupported", result.stdout)
        self.assertNotIn("qemu-system-x86_64", result.stdout)

    def test_kvm_probe_rejects_an_unusable_device(self) -> None:
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            temporary = Path(directory)
            device = temporary / "kvm"
            device.touch()
            qemu = temporary / "qemu"
            qemu.write_text("#!/bin/sh\nexit 1\n", encoding="utf-8")
            qemu.chmod(0o755)
            command = (
                f'. "{ROOT / "scripts/lib/host.sh"}"; '
                f'host_kvm_available "{qemu}" "{device}"'
            )
            result = subprocess.run(
                ["/usr/bin/bash", "-c", command],
                check=False,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )

        self.assertNotEqual(result.returncode, 0, result.stdout)


if __name__ == "__main__":
    unittest.main()
