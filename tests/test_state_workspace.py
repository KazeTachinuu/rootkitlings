from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import stat
import tempfile
import unittest

from engine.state import (
    ExerciseState,
    StateError,
    StateStore,
    dump_state,
    load_state,
    new_state,
)
from engine.workspace import (
    WorkspaceError,
    archive_and_remove_workspaces,
    archive_workspace,
    canonical_tree_hash,
    clean_paths,
    create_snapshot,
    materialize,
    restore_workspace,
    verify_snapshot,
)


ROUTE = ("module-core", "proc-status")
REVISION = "state-test"
PREREQUISITES = {
    "module-core": frozenset(),
    "proc-status": frozenset({"module-core"}),
}


def _increment_attempts(state_path: str, count: int) -> None:
    store = StateStore(Path(state_path), ROUTE, REVISION, PREREQUISITES)
    for _ in range(count):
        with store.locked() as state:
            state.exercise("module-core").attempts += 1


class StateTests(unittest.TestCase):
    def test_new_state_derives_current(self) -> None:
        state = new_state(ROUTE, REVISION, PREREQUISITES)
        self.assertEqual(state.current, "module-core")
        state.completed.add("module-core")
        self.assertEqual(state.current, "proc-status")
        state.completed.add("proc-status")
        self.assertIsNone(state.current)

    def test_round_trip_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "state.toml"
            store = StateStore(path, ROUTE, REVISION, PREREQUISITES)
            with store.locked() as state:
                state.completed.add("module-core")
                state.exercises["proc-status"] = ExerciseState(attempts=2)
            first = path.read_bytes()
            loaded = load_state(path, ROUTE, REVISION, PREREQUISITES)
            self.assertEqual(loaded.current, "proc-status")
            self.assertEqual(loaded.exercises["proc-status"].attempts, 2)
            with store.locked():
                pass
            self.assertEqual(first, path.read_bytes())
            self.assertNotIn(b"timestamp", first)

    def test_failed_locked_update_is_not_written(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "state.toml"
            store = StateStore(path, ROUTE, REVISION, PREREQUISITES)
            with self.assertRaisesRegex(RuntimeError, "stop"):
                with store.locked() as state:
                    state.completed.add("module-core")
                    raise RuntimeError("stop")
            self.assertFalse(path.exists())

    def test_revision_route_and_unknown_id_are_rejected(self) -> None:
        state = new_state(ROUTE, REVISION, PREREQUISITES)
        with self.assertRaisesRegex(StateError, "revision mismatch"):
            state.validate(ROUTE, "other", PREREQUISITES)
        with self.assertRaisesRegex(StateError, "not earlier"):
            state.validate(tuple(reversed(ROUTE)), REVISION, PREREQUISITES)
        state.exercises["unknown"] = ExerciseState()
        with self.assertRaisesRegex(StateError, "unknown stable IDs"):
            state.validate(ROUTE, REVISION, PREREQUISITES)

    def test_clear_progress_removes_only_selected_state(self) -> None:
        state = new_state(ROUTE, REVISION, PREREQUISITES)
        state.completed.update(ROUTE)
        for stable_id in ROUTE:
            state.exercise(stable_id).attempts = 1
        state.clear_progress({"proc-status"})
        self.assertEqual(state.completed, {"module-core"})
        self.assertEqual(set(state.exercises), {"module-core"})

    def test_required_completion_must_be_prerequisite_closed(self) -> None:
        state = new_state(ROUTE, REVISION, PREREQUISITES)
        state.completed.add("proc-status")
        with self.assertRaisesRegex(StateError, "lacks prerequisites"):
            state.validate(ROUTE, REVISION, PREREQUISITES)

    def test_read_locked_does_not_create_or_rewrite_state(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "state.toml"
            store = StateStore(path, ROUTE, REVISION, PREREQUISITES)
            with store.read_locked() as state:
                self.assertEqual(state.current, "module-core")
            self.assertFalse(path.exists())
            with store.locked() as state:
                state.completed.add("module-core")
            before = path.read_bytes()
            with store.read_locked() as state:
                self.assertEqual(state.current, "proc-status")
            self.assertEqual(path.read_bytes(), before)

    def test_invalid_hash_and_unknown_toml_field_are_rejected(self) -> None:
        state = new_state(ROUTE, REVISION, PREREQUISITES)
        state.exercise("module-core").workspace_hash = "bad"
        with self.assertRaisesRegex(StateError, "invalid content hash"):
            state.validate(ROUTE, REVISION, PREREQUISITES)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "state.toml"
            content = dump_state(new_state(ROUTE, REVISION, PREREQUISITES))
            path.write_text(content.replace("\n[exercises]\n", "\nextra = 1\n\n[exercises]\n"))
            with self.assertRaisesRegex(StateError, "top-level fields"):
                load_state(path, ROUTE, REVISION, PREREQUISITES)

    def test_lock_serializes_concurrent_updates(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "state.toml"
            workers = 3
            increments = 12
            with ThreadPoolExecutor(max_workers=workers) as executor:
                futures = [
                    executor.submit(_increment_attempts, str(path), increments)
                    for _ in range(workers)
                ]
                for future in futures:
                    future.result(timeout=10)
            state = load_state(path, ROUTE, REVISION, PREREQUISITES)
            self.assertEqual(
                state.exercise("module-core").attempts,
                workers * increments,
            )
            leftovers = list(path.parent.glob(".state.toml.*"))
            self.assertEqual(leftovers, [])


class WorkspaceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.starters = self.root / "starters"
        self.work = self.root / "work"
        self.archive = self.root / "archive"
        self.passed = self.root / "passed"
        for path in (self.starters, self.work, self.archive, self.passed):
            path.mkdir()
        self.starter = self.starters / "module-core"
        (self.starter / "src").mkdir(parents=True)
        (self.starter / "src/main.c").write_text("int value = 1;\n")
        (self.starter / "Kbuild").write_text("obj-m += hello.o\n")
        self.files = ("Kbuild", "src/main.c")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_hash_is_order_independent_and_name_sensitive(self) -> None:
        first = canonical_tree_hash(self.starter, self.files)
        second = canonical_tree_hash(self.starter, reversed(self.files))
        self.assertEqual(first, second)
        other = self.root / "other"
        (other / "src").mkdir(parents=True)
        (other / "src/renamed.c").write_text("int value = 1;\n")
        (other / "Kbuild").write_text("obj-m += hello.o\n")
        self.assertNotEqual(first, canonical_tree_hash(other, ("Kbuild", "src/renamed.c")))

    def test_hash_is_executable_mode_sensitive(self) -> None:
        before = canonical_tree_hash(self.starter, self.files)
        (self.starter / "src/main.c").chmod(0o755)
        after = canonical_tree_hash(self.starter, self.files)
        self.assertNotEqual(before, after)

    def test_materialize_copies_only_declared_files_once(self) -> None:
        (self.starter / "not-declared.txt").write_text("ignore\n")
        workspace = materialize(self.starter, self.work, "module-core", self.files)
        self.assertEqual((workspace / "src/main.c").read_text(), "int value = 1;\n")
        self.assertFalse((workspace / "not-declared.txt").exists())
        with self.assertRaisesRegex(WorkspaceError, "already exists"):
            materialize(self.starter, self.work, "module-core", self.files)

    def test_materialize_accepts_serial_names_but_rejects_unsafe_names(self) -> None:
        workspace = materialize(self.starter, self.work, "L00", self.files)
        self.assertEqual(workspace, self.work / "L00")
        for name in ("../L00", "L00/source", ".", "_L00"):
            with self.subTest(name=name):
                with self.assertRaisesRegex(
                    WorkspaceError,
                    "invalid generated-tree name",
                ):
                    materialize(self.starter, self.work, name, self.files)

    def test_materialize_normalizes_snapshot_style_modes_for_learning(self) -> None:
        (self.starter / "Kbuild").chmod(0o444)
        (self.starter / "src/main.c").chmod(0o555)
        workspace = materialize(self.starter, self.work, "module-core", self.files)
        self.assertEqual(stat.S_IMODE((workspace / "Kbuild").stat().st_mode), 0o644)
        self.assertEqual(
            stat.S_IMODE((workspace / "src/main.c").stat().st_mode),
            0o755,
        )

    def test_paths_symlinks_and_special_files_are_rejected(self) -> None:
        with self.assertRaisesRegex(WorkspaceError, "unsafe relative"):
            canonical_tree_hash(self.starter, ("../outside",))
        (self.starter / "link").symlink_to("src/main.c")
        with self.assertRaisesRegex(WorkspaceError, "symlink"):
            canonical_tree_hash(self.starter, ("link",))
        fifo = self.starter / "fifo"
        os.mkfifo(fifo)
        try:
            with self.assertRaisesRegex(WorkspaceError, "regular file"):
                canonical_tree_hash(self.starter, ("fifo",))
        finally:
            fifo.unlink()
        starter_link = self.root / "starter-link"
        starter_link.symlink_to(self.starter, target_is_directory=True)
        with self.assertRaisesRegex(WorkspaceError, "must not be a symlink"):
            materialize(starter_link, self.work, "proc-status", self.files)

    def test_snapshot_manifest_detects_tampering_and_extra_files(self) -> None:
        workspace = materialize(self.starter, self.work, "module-core", self.files)
        tree_hash, snapshot = create_snapshot(workspace, self.passed, self.files)
        self.assertEqual(verify_snapshot(snapshot), tree_hash)
        self.assertEqual(stat.S_IMODE(snapshot.stat().st_mode), 0o555)
        (snapshot / "src/main.c").chmod(0o644)
        (snapshot / "src/main.c").write_text("tampered\n")
        with self.assertRaisesRegex(WorkspaceError, "verification failed"):
            verify_snapshot(snapshot, tree_hash)

    def test_snapshot_rejects_undeclared_extra_file(self) -> None:
        workspace = materialize(self.starter, self.work, "module-core", self.files)
        tree_hash, snapshot = create_snapshot(workspace, self.passed, self.files)
        snapshot.chmod(0o755)
        (snapshot / "extra").write_text("unexpected")
        with self.assertRaisesRegex(WorkspaceError, "undeclared files"):
            verify_snapshot(snapshot, tree_hash)

    def test_snapshot_preserves_and_verifies_executable_class(self) -> None:
        (self.starter / "src/main.c").chmod(0o755)
        workspace = materialize(self.starter, self.work, "module-core", self.files)
        tree_hash, snapshot = create_snapshot(workspace, self.passed, self.files)
        self.assertTrue((snapshot / "src/main.c").stat().st_mode & 0o111)
        self.assertEqual(verify_snapshot(snapshot), tree_hash)
        (snapshot / "src/main.c").chmod(0o444)
        with self.assertRaisesRegex(WorkspaceError, "mode verification"):
            verify_snapshot(snapshot, tree_hash)

    def test_archive_is_content_addressed_and_idempotent(self) -> None:
        workspace = materialize(self.starter, self.work, "module-core", self.files)
        first = archive_workspace(workspace, self.archive, "module-core")
        second = archive_workspace(workspace, self.archive, "module-core")
        self.assertEqual(first, second)
        self.assertIn(canonical_tree_hash(workspace, self.files), first.name)

    def test_existing_archive_rejects_extra_file(self) -> None:
        workspace = materialize(self.starter, self.work, "module-core", self.files)
        archive = archive_workspace(workspace, self.archive, "module-core")
        (archive / "extra").write_text("corrupt")
        with self.assertRaisesRegex(WorkspaceError, "undeclared"):
            archive_workspace(workspace, self.archive, "module-core")

    def test_restore_archives_then_restores_baseline(self) -> None:
        (self.starter / "src/main.c").chmod(0o755)
        workspace = materialize(self.starter, self.work, "module-core", self.files)
        baseline_hash, baseline = create_snapshot(workspace, self.passed, self.files)
        (workspace / "src/main.c").write_text("learner edit\n")
        restored, archived = restore_workspace(self.work, self.archive, "module-core", baseline, self.files)
        self.assertEqual(canonical_tree_hash(restored, self.files), baseline_hash)
        self.assertEqual((archived / "src/main.c").read_text(), "learner edit\n")
        self.assertEqual(
            stat.S_IMODE((restored / "src/main.c").stat().st_mode),
            0o755,
        )
        self.assertEqual(stat.S_IMODE((restored / "Kbuild").stat().st_mode), 0o644)

    def test_archive_and_remove_is_safe_when_some_workspaces_are_absent(self) -> None:
        workspace = materialize(self.starter, self.work, "module-core", self.files)
        (workspace / "src/main.c").write_text("learner edit\n")
        archives = archive_and_remove_workspaces(
            self.work,
            self.archive,
            ("module-core", "absent-workspace"),
        )
        self.assertEqual(set(archives), {"module-core"})
        self.assertFalse(workspace.exists())
        self.assertEqual(
            (archives["module-core"] / "src/main.c").read_text(),
            "learner edit\n",
        )

    def test_clean_removes_only_declared_outputs(self) -> None:
        workspace = materialize(self.starter, self.work, "module-core", self.files)
        (workspace / "build").mkdir()
        (workspace / "build/hello.o").write_bytes(b"object")
        (workspace / "keep.txt").write_text("keep")
        clean_paths(workspace, ("build", "missing"))
        self.assertFalse((workspace / "build").exists())
        self.assertTrue((workspace / "keep.txt").exists())

    def test_clean_rejects_symlink(self) -> None:
        workspace = materialize(self.starter, self.work, "module-core", self.files)
        (workspace / "output").symlink_to(self.root)
        with self.assertRaisesRegex(WorkspaceError, "symlink"):
            clean_paths(workspace, ("output",))

    def test_restore_rejects_workspace_symlink(self) -> None:
        outside = self.root / "outside"
        outside.mkdir()
        (outside / "Kbuild").write_text("outside")
        (outside / "src").mkdir()
        (outside / "src/main.c").write_text("outside")
        (self.work / "module-core").symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(WorkspaceError, "must not be a symlink"):
            restore_workspace(self.work, self.archive, "module-core", self.starter, self.files)


if __name__ == "__main__":
    unittest.main()
