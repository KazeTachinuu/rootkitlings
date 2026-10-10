from __future__ import annotations

from pathlib import Path
import tempfile
import unittest
from unittest import mock

from engine.compile_database import (
    CompileDatabaseError,
    base_compile_entry,
    bundle_source_files,
    captured_database,
    expanded_entry,
    learner_source_files,
    source_include_directories,
)
from engine.catalog import Catalog


class CompileDatabaseTests(unittest.TestCase):
    def test_missing_bear_has_one_direct_optional_dependency_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            with mock.patch("engine.compile_database.shutil.which", return_value=None):
                with self.assertRaisesRegex(
                    CompileDatabaseError,
                    "Bear is required; install the 'bear' package",
                ):
                    captured_database(mock.Mock(), Path(directory))

    def test_discovers_only_manifest_owned_starter_and_solution_sources(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bundle = root / "curriculum/L99-example"
            (bundle / "starter").mkdir(parents=True)
            (bundle / "solution").mkdir()
            (bundle / "fixtures").mkdir()
            (bundle / "exercise.toml").write_text("schema_version = 2\n")
            starter = bundle / "starter/main.c"
            solution = bundle / "solution/main.c"
            fixture = bundle / "fixtures/harness.c"
            starter.write_text("int starter;\n")
            solution.write_text("int solution;\n")
            fixture.write_text("int fixture;\n")

            unowned = root / "curriculum/unowned/starter"
            unowned.mkdir(parents=True)
            (unowned / "ignored.c").write_text("int ignored;\n")

            self.assertEqual(bundle_source_files(root), {starter, solution})

    def test_discovers_real_learner_sources_without_following_symlinks(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            workspace = root / "work/L00"
            workspace.mkdir(parents=True)
            source = workspace / "main.c"
            source.write_text("int learner;\n")
            outside = root / "outside.c"
            outside.write_text("int outside;\n")
            (workspace / "linked.c").symlink_to(outside)

            self.assertEqual(learner_source_files(root), {source})

    def test_selects_only_the_captured_reference_source(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "core/main.c"
            source.parent.mkdir()
            source.write_text("int main;\n")
            wanted = {"directory": str(root), "file": "core/main.c", "arguments": []}
            generated = {"directory": str(root), "file": "rootkitlings.mod.c"}
            self.assertIs(base_compile_entry([wanted, generated], {source}), wanted)
            with self.assertRaises(CompileDatabaseError):
                base_compile_entry([generated], {source})

    def test_expansion_uses_stable_paths_and_bundle_includes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bundle = root / "curriculum/L99-example"
            source = bundle / "solution/core/main.c"
            solution_include = bundle / "solution/include"
            starter_include = bundle / "starter/include"
            source.parent.mkdir(parents=True)
            solution_include.mkdir(parents=True)
            starter_include.mkdir(parents=True)
            (bundle / "starter/Kbuild").write_text(
                "ccflags-y += -DROOTKITLINGS_FAIL_POINT=$(ROOTKITLINGS_FAIL_POINT)\n"
            )
            source.write_text("int main;\n")
            reference = root / "reference/core/main.c"
            reference.parent.mkdir(parents=True)
            reference.write_text("int reference;\n")
            base = {
                "directory": str(root / "reference"),
                "file": "core/main.c",
                "output": "core/main.o",
                "arguments": [
                    "gcc",
                    "-Wp,-MMD,core/.main.o.d",
                    '-DKBUILD_BASENAME="main"',
                    "-c",
                    "-o",
                    "core/main.o",
                    "core/main.c",
                ],
            }

            entry = expanded_entry(base, source, root)

            self.assertEqual(entry["directory"], str(root.resolve()))
            self.assertEqual(entry["file"], str(source.resolve()))
            self.assertNotIn("output", entry)
            arguments = entry["arguments"]
            self.assertNotIn("-o", arguments)
            self.assertFalse(any(value.startswith("-Wp,-MMD,") for value in arguments))
            self.assertIn(f"-I{solution_include.resolve()}", arguments)
            self.assertIn(f"-I{starter_include.resolve()}", arguments)
            self.assertIn("-DROOTKITLINGS_FAIL_POINT=0", arguments)
            self.assertIn('-DKBUILD_BASENAME="main"', arguments)
            self.assertEqual(arguments[-2:], ["-c", str(source.resolve())])

    def test_cumulative_source_includes_provider_headers(self) -> None:
        root = Path(__file__).resolve().parent.parent
        catalog = Catalog(root)
        source = root / "curriculum/C00-complete-rootkit/solution/security/control.c"
        expected = root / "curriculum/M07-remote-resident-agent/starter/include"

        includes = source_include_directories(source, root, catalog)

        self.assertIn(expected.resolve(), includes)

    def test_learner_source_uses_its_workspace_include_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "work/L00/core/main.c"
            include = root / "work/L00/include"
            source.parent.mkdir(parents=True)
            include.mkdir(parents=True)
            source.write_text("int learner;\n")

            self.assertEqual(
                source_include_directories(source, root),
                (include.resolve(),),
            )


if __name__ == "__main__":
    unittest.main()
