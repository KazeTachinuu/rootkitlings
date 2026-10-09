from pathlib import Path
import os
import tempfile
import unittest

from engine.solutions import (
    full_solution_tree,
    import_solution_files,
    materialize_solution_tree,
    overlay_solution_tree,
)
from engine.workspace import WorkspaceError


class SolutionCompositionTests(unittest.TestCase):
    def write(self, root: Path, relative: str, data: str) -> None:
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(data, encoding="utf-8")

    def test_recursive_sparse_overlay_composition_and_materialization(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root / "base"
            first = root / "first"
            second = root / "second"
            output = root / "output"
            for path in (base, first, second, output):
                path.mkdir()
            self.write(base, "Kbuild", "base build\n")
            self.write(base, "core/main.c", "base main\n")
            tree = full_solution_tree(base, ("Kbuild", "core/main.c"))
            self.write(first, "core/main.c", "first main\n")
            self.write(first, "include/api.h", "api\n")
            tree = overlay_solution_tree(
                tree,
                first,
                ("Kbuild", "core/main.c"),
                ("Kbuild", "core/main.c", "include/api.h"),
            )
            self.write(second, "Kbuild", "second build\n")
            tree = overlay_solution_tree(
                tree,
                second,
                ("Kbuild", "core/main.c", "include/api.h"),
                ("Kbuild", "core/main.c", "include/api.h"),
            )
            result = materialize_solution_tree(tree, output, "composed")
            self.assertEqual((result / "Kbuild").read_text(), "second build\n")
            self.assertEqual((result / "core/main.c").read_text(), "first main\n")
            self.assertEqual((result / "include/api.h").read_text(), "api\n")

    def test_full_solution_must_be_exact(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.write(root, "main.c", "main\n")
            self.write(root, "stray.c", "stray\n")
            with self.assertRaisesRegex(WorkspaceError, "exactly the declared"):
                full_solution_tree(root, ("main.c",))

    def test_overlay_must_be_nonempty_and_include_new_files(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root / "base"
            overlay = root / "overlay"
            base.mkdir()
            overlay.mkdir()
            self.write(base, "main.c", "main\n")
            provider = full_solution_tree(base, ("main.c",))
            with self.assertRaisesRegex(WorkspaceError, "must not be empty"):
                overlay_solution_tree(provider, overlay, ("main.c",), ("main.c",))
            self.write(overlay, "main.c", "changed\n")
            with self.assertRaisesRegex(WorkspaceError, "every newly declared"):
                overlay_solution_tree(
                    provider,
                    overlay,
                    ("main.c",),
                    ("main.c", "new.c"),
                )

    def test_overlay_rejects_identical_undeclared_and_unsafe_entries(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root / "base"
            base.mkdir()
            self.write(base, "main.c", "main\n")
            provider = full_solution_tree(base, ("main.c",))

            identical = root / "identical"
            identical.mkdir()
            self.write(identical, "main.c", "main\n")
            with self.assertRaisesRegex(WorkspaceError, "byte-identical"):
                overlay_solution_tree(provider, identical, ("main.c",), ("main.c",))

            undeclared = root / "undeclared"
            undeclared.mkdir()
            self.write(undeclared, "stray.c", "stray\n")
            with self.assertRaisesRegex(WorkspaceError, "undeclared"):
                overlay_solution_tree(provider, undeclared, ("main.c",), ("main.c",))

            linked = root / "linked"
            linked.mkdir()
            os.symlink(base / "main.c", linked / "main.c")
            with self.assertRaisesRegex(WorkspaceError, "symlink"):
                overlay_solution_tree(provider, linked, ("main.c",), ("main.c",))

            empty_directory = root / "empty-directory"
            empty_directory.mkdir()
            (empty_directory / "empty").mkdir()
            with self.assertRaisesRegex(WorkspaceError, "stray or empty"):
                overlay_solution_tree(
                    provider,
                    empty_directory,
                    ("main.c",),
                    ("main.c",),
                )

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

    def test_imports_select_and_rename_composed_provider_files(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base_root = root / "base"
            provider_root = root / "provider"
            base_root.mkdir()
            provider_root.mkdir()
            self.write(base_root, "main.c", "main\n")
            self.write(provider_root, "include/api.h", "api\n")
            self.write(provider_root, "shared.c", "shared\n")
            base = full_solution_tree(base_root, ("main.c",))
            provider = full_solution_tree(
                provider_root,
                ("include/api.h", "shared.c"),
            )

            result = import_solution_files(
                base,
                ((
                    "provider",
                    provider,
                    (
                        ("include/api.h", "vendor/api.h"),
                        ("shared.c", "shared.c"),
                    ),
                ),),
                ("main.c", "vendor/api.h", "shared.c"),
            )

            self.assertEqual(result["vendor/api.h"].data, b"api\n")
            self.assertEqual(result["shared.c"].data, b"shared\n")

    def test_imports_reject_missing_sources_collisions_and_incomplete_trees(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base_root = root / "base"
            provider_root = root / "provider"
            base_root.mkdir()
            provider_root.mkdir()
            self.write(base_root, "main.c", "main\n")
            self.write(provider_root, "shared.c", "shared\n")
            base = full_solution_tree(base_root, ("main.c",))
            provider = full_solution_tree(provider_root, ("shared.c",))

            with self.assertRaisesRegex(WorkspaceError, "no source file"):
                import_solution_files(
                    base,
                    (("provider", provider, (("missing.c", "copy.c"),)),),
                    ("main.c", "copy.c"),
                )
            with self.assertRaisesRegex(WorkspaceError, "collides"):
                import_solution_files(
                    base,
                    (("provider", provider, (("shared.c", "main.c"),)),),
                    ("main.c",),
                )
            with self.assertRaisesRegex(WorkspaceError, "do not cover"):
                import_solution_files(base, (), ("main.c", "copy.c"))

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
