from pathlib import Path
import runpy
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "scripts/expand-compile-db"


class ExpandCompileDatabaseTests(unittest.TestCase):
    def test_discovers_only_manifest_owned_starter_and_solution_sources(self) -> None:
        namespace = runpy.run_path(str(SCRIPT), run_name="validation_test")
        discover = namespace["bundle_source_files"]
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

            self.assertEqual(discover(root), {starter, solution})


if __name__ == "__main__":
    unittest.main()
