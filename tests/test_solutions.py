from pathlib import Path
import tempfile
import unittest

from engine.solutions import (
    full_solution_tree,
    materialize_solution_tree,
)
from engine.workspace import WorkspaceError


class SolutionCompositionTests(unittest.TestCase):
    def write(self, root: Path, relative: str, data: str) -> None:
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(data, encoding="utf-8")

    def test_full_solution_must_be_exact(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.write(root, "main.c", "main\n")
            self.write(root, "stray.c", "stray\n")
            with self.assertRaisesRegex(WorkspaceError, "exactly the declared"):
                full_solution_tree(root, ("main.c",))

    def test_materialization_rejects_existing_destination(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root / "base"
            output = root / "output"
            base.mkdir()
            output.mkdir()
            self.write(base, "main.c", "main\n")
            (output / "exercise").mkdir()
            tree = full_solution_tree(base, ("main.c",))
            with self.assertRaisesRegex(WorkspaceError, "already exists"):
                materialize_solution_tree(tree, output, "exercise")

    def test_modes_are_normalized_from_executable_bit_only(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root / "base"
            output = root / "output"
            base.mkdir()
            output.mkdir()
            self.write(base, "plain.c", "plain\n")
            self.write(base, "check", "#!/bin/sh\n")
            (base / "plain.c").chmod(0o666)
            (base / "check").chmod(0o711)
            tree = full_solution_tree(base, ("plain.c", "check"))
            self.assertEqual(tree["plain.c"].mode, 0o644)
            self.assertEqual(tree["check"].mode, 0o755)
            result = materialize_solution_tree(tree, output, "exercise")
            self.assertEqual((result / "plain.c").stat().st_mode & 0o777, 0o644)
            self.assertEqual((result / "check").stat().st_mode & 0o777, 0o755)


if __name__ == "__main__":
    unittest.main()
