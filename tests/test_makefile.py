from __future__ import annotations

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parent.parent


def make(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["make", "--no-print-directory", "-C", str(ROOT), *arguments],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )


class MakefileTests(unittest.TestCase):
    def test_default_all_and_help_are_the_same_documented_surface(self) -> None:
        outputs = []
        for arguments in ((), ("all",), ("help",)):
            result = make(*arguments)
            self.assertEqual(result.returncode, 0, result.stdout)
            outputs.append(result.stdout)
        self.assertEqual(outputs[0], outputs[1])
        self.assertEqual(outputs[1], outputs[2])
        for target in (
            "tools", "clean", "doctor", "validate", "validate-quick",
            "test-solutions", "compile-db",
        ):
            self.assertIn(target, outputs[0])
        self.assertNotIn("module", outputs[0])

    def test_missing_module_directory_never_enters_the_kernel_makefile(self) -> None:
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            fake_kernel = Path(directory)
            marker = fake_kernel / "included-makefile-was-remade"
            (fake_kernel / "Makefile").write_text(
                "include generated.mk\n"
                "generated.mk:\n"
                f"\t@printf touched > {marker}\n"
                "\t@printf 'modules:\\n' > generated.mk\n",
                encoding="utf-8",
            )
            for dry_run in (False, True):
                with self.subTest(dry_run=dry_run):
                    arguments = [f"KDIR={fake_kernel}"]
                    if dry_run:
                        arguments.append("-n")
                    arguments.append("module")
                    result = make(*arguments)
                    if not dry_run:
                        self.assertEqual(result.returncode, 2, result.stdout)
                        self.assertIn("MODULE_DIR is required", result.stdout)
                    self.assertFalse(marker.exists(), result.stdout)

    def test_module_forwards_the_controlled_build_contract(self) -> None:
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            temporary = Path(directory)
            fake_kernel = temporary / "kernel"
            module = temporary / "module"
            fake_kernel.mkdir()
            module.mkdir()
            record = temporary / "record"
            (fake_kernel / "Makefile").write_text(
                "modules:\n"
                f"\t@printf '%s\\n' '$(M)|$(ARCH)|$(CC)|$(HOSTCC)|$(W)|$(ROOTKITLINGS_FAIL_POINT)' > {record}\n",
                encoding="utf-8",
            )
            result = make(
                f"KDIR={fake_kernel}",
                f"MODULE_DIR={module}",
                "ARCH=arm64",
                "CC=false",
                "HOSTCC=false",
                "ROOTKITLINGS_FAIL_POINT=7",
                "module",
            )
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertEqual(
                record.read_text(encoding="utf-8").strip(),
                f"{module}|x86_64|gcc|gcc|1|7",
            )

    def test_tool_output_rejects_empty_root_and_custom_clean_paths(self) -> None:
        for output in ("", "/"):
            with self.subTest(output=output):
                result = make(f"TOOL_OUT={output}", "tool-output-directory")
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn("TOOL_OUT must name a directory", result.stdout)

        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            output = Path(directory) / "tools"
            output.mkdir()
            sentinel = output / "sentinel"
            sentinel.write_text("keep\n", encoding="utf-8")
            result = make(f"TOOL_OUT={output}", "clean")
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertTrue(sentinel.is_file())

    def test_one_guest_tool_builds_in_an_isolated_output_directory(self) -> None:
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            output = Path(directory) / "tools"
            target = output / "agent_fixture"
            result = make(f"TOOL_OUT={output}", str(target))
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertTrue(target.is_file())
            self.assertTrue(target.stat().st_mode & 0o111)


if __name__ == "__main__":
    unittest.main()
