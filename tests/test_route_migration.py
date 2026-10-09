from __future__ import annotations

import hashlib
from pathlib import Path
import runpy
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from engine.migration import CurriculumContract, MigrationError
from engine.route_migration import (
    DESTINATION_CONTRACT,
    DESTINATION_DIRECT_PREREQUISITES,
    DESTINATION_REVISION,
    DESTINATION_ROUTE,
    HISTORY_DESTINATIONS,
    SOURCE_CONTRACT,
    SOURCE_DIRECT_PREREQUISITES,
    SOURCE_REVISION,
    SOURCE_ROUTE,
    migrate_core_route,
)
from engine.state import ExerciseState, LearnerState, dump_state, parse_state
from engine.workspace import WorkspaceError, canonical_tree_hash


HASH_A = "a" * 64
HASH_B = "b" * 64


def source_state(*, full: bool = False) -> LearnerState:
    completed = set(SOURCE_ROUTE) if full else set(SOURCE_ROUTE[:12])
    return LearnerState(
        SOURCE_REVISION,
        SOURCE_ROUTE,
        completed,
        {
            "proc-read": ExerciseState(
                HASH_A, HASH_A, HASH_A, 3, True,
            ),
            "kernel-debugging": ExerciseState(
                HASH_B, HASH_B, HASH_B, 5, True,
            ),
            "ghost-process": ExerciseState(attempts=11, solution_revealed=True),
            "sha256-verification": ExerciseState(attempts=7),
            "authenticated-encryption": ExerciseState(attempts=13),
        },
    )


def schema2_bytes(state: LearnerState) -> bytes:
    lines = dump_state(state).splitlines()
    lines[0] = "schema_version = 2"
    lines = [line for line in lines if not line.startswith("baseline_provider = ")]
    return ("\n".join(lines) + "\n").encode()


class RouteMigrationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.state_path = self.root / "state.toml"
        self.work_root = self.root / "work"
        self.archive_root = self.root / "archive"
        self.passed_root = self.root / "passed"
        for path in (self.work_root, self.archive_root, self.passed_root):
            path.mkdir()

    def write_workspace(self, stable_id: str, content: bytes = b"learner edits\n") -> Path:
        workspace = self.work_root / stable_id
        workspace.mkdir()
        (workspace / "source.c").write_bytes(content)
        return workspace

    def migrate(self) -> bool:
        return migrate_core_route(
            self.state_path,
            self.work_root,
            self.archive_root,
            DESTINATION_CONTRACT,
        )

    def test_frozen_contracts_cover_exact_routes_and_direct_dependencies(self) -> None:
        self.assertEqual(len(SOURCE_ROUTE), 65)
        self.assertEqual(len(set(SOURCE_ROUTE)), 65)
        self.assertEqual(set(SOURCE_DIRECT_PREREQUISITES), set(SOURCE_ROUTE))
        self.assertEqual(SOURCE_CONTRACT.ids, frozenset(SOURCE_ROUTE))
        self.assertEqual(len(DESTINATION_ROUTE), 36)
        self.assertEqual(len(set(DESTINATION_ROUTE)), 36)
        self.assertEqual(
            set(DESTINATION_DIRECT_PREREQUISITES), set(DESTINATION_ROUTE),
        )
        self.assertEqual(DESTINATION_CONTRACT.ids, frozenset(DESTINATION_ROUTE))
        self.assertEqual(
            DESTINATION_DIRECT_PREREQUISITES["authenticated-resident-agent"],
            (
                "remote-resident-agent", "authenticated-encryption",
                "authenticated-dns-signal",
            ),
        )
        self.assertEqual(
            DESTINATION_CONTRACT.prerequisites["controlled-resident-core"],
            frozenset({
                "timer-workqueue-scheduler", "event-driven-kernel-thread",
                "proc-publication",
            }),
        )

    def test_schema3_full_state_gets_no_credit_or_hashes_but_keeps_safe_history(self) -> None:
        original = dump_state(source_state(full=True)).encode()
        self.state_path.write_bytes(original)
        self.write_workspace("proc-read")
        self.write_workspace("authenticated-encryption", b"old same-id workspace\n")
        passed_marker = self.passed_root / HASH_A
        passed_marker.mkdir()
        (passed_marker / "snapshot.toml").write_text("leave me alone\n")

        self.assertTrue(self.migrate())

        migrated = parse_state(self.state_path.read_text())
        self.assertEqual(migrated.curriculum_revision, DESTINATION_REVISION)
        self.assertEqual(migrated.route, DESTINATION_ROUTE)
        self.assertEqual(migrated.completed, set())
        self.assertEqual(migrated.exercises["proc-read-contract"].attempts, 3)
        self.assertTrue(migrated.exercises["proc-read-contract"].solution_revealed)
        self.assertEqual(migrated.exercises["sha256-verification"].attempts, 7)
        self.assertEqual(migrated.exercises["concealment-evidence-map"].attempts, 11)
        self.assertNotIn("kernel-debugging", migrated.exercises)
        self.assertNotIn("authenticated-encryption", migrated.exercises)
        for record in migrated.exercises.values():
            self.assertEqual(record.baseline_hash, "")
            self.assertEqual(record.baseline_provider, "")
            self.assertEqual(record.workspace_hash, "")
            self.assertEqual(record.passed_snapshot, "")
        self.assertFalse((self.work_root / "proc-read").exists())
        self.assertFalse((self.work_root / "authenticated-encryption").exists())
        self.assertEqual(
            (passed_marker / "snapshot.toml").read_text(), "leave me alone\n",
        )
        digest = hashlib.sha256(original).hexdigest()
        self.assertEqual(
            (self.archive_root / f"state-route-v2-{digest}.toml").read_bytes(),
            original,
        )
        workspace_archives = [
            path.name for path in self.archive_root.iterdir()
            if path.is_dir()
        ]
        self.assertEqual(len(workspace_archives), 2)
        self.assertTrue(any(name.startswith("proc-read-") for name in workspace_archives))
        self.assertTrue(
            any(name.startswith("authenticated-encryption-") for name in workspace_archives)
        )
        self.assertFalse(self.migrate())

    def test_schema2_partial_state_is_accepted_only_with_the_exact_source_route(self) -> None:
        original = schema2_bytes(source_state())
        self.state_path.write_bytes(original)

        self.assertTrue(self.migrate())

        migrated = parse_state(self.state_path.read_text())
        self.assertEqual(migrated.completed, set())
        self.assertEqual(migrated.exercises["proc-read-contract"].attempts, 3)
        self.assertEqual(migrated.exercises["sha256-verification"].attempts, 7)
        self.assertNotIn("kernel-debugging", migrated.exercises)

        self.state_path.write_bytes(
            original.replace(
                (", \"operation-glasshouse\"]").encode(), b"]", 1,
            )
        )
        with self.assertRaisesRegex(MigrationError, "frozen source contract"):
            self.migrate()

    def test_history_map_is_one_to_one_and_never_uses_serial_aliases(self) -> None:
        self.assertEqual(len(HISTORY_DESTINATIONS), len(set(HISTORY_DESTINATIONS.values())))
        self.assertTrue(set(HISTORY_DESTINATIONS) <= set(SOURCE_ROUTE))
        self.assertTrue(set(HISTORY_DESTINATIONS.values()) <= set(DESTINATION_ROUTE))
        for source, destination in HISTORY_DESTINATIONS.items():
            self.assertFalse(source.startswith(("L", "M", "C")))
            self.assertFalse(destination.startswith(("L", "M", "C")))

    def test_missing_state_does_not_touch_an_orphaned_workspace_or_archive(self) -> None:
        workspace = self.write_workspace("proc-read")

        self.assertFalse(self.migrate())

        self.assertTrue(workspace.exists())
        self.assertEqual(list(self.archive_root.iterdir()), [])

    def test_cli_does_not_invoke_route_migration_for_the_live_v2_revision(self) -> None:
        cli_path = Path(__file__).resolve().parent.parent / "rootkitlings"
        namespace = runpy.run_path(cli_path, run_name="route_migration_wiring_test")
        globals_ = namespace["initialize_runtime"].__globals__
        runtime = self.root / "cli-runtime"
        values = {
            "RT": runtime,
            "STATE": runtime / "state.toml",
            "WORK": runtime / "work",
            "PASSED": runtime / "passed",
            "ARCHIVE": runtime / "archive",
        }
        globals_.update(values)
        globals_["STORE"] = globals_["StateStore"](
            values["STATE"],
            set(globals_["CAT"].by_id),
            globals_["ROUTE"],
            globals_["CAT"].revision,
            globals_["COMPLETION_DEPENDENCIES"],
            globals_["BASELINE_PROVIDERS"],
        )
        migration = Mock()
        globals_["migrate_core_route"] = migration

        globals_["initialize_runtime"]()

        self.assertEqual(globals_["CAT"].revision, SOURCE_REVISION)
        migration.assert_not_called()

    def test_cli_invokes_route_migration_only_with_the_exact_active_contract(self) -> None:
        cli_path = Path(__file__).resolve().parent.parent / "rootkitlings"
        namespace = runpy.run_path(cli_path, run_name="route_migration_wiring_test")
        globals_ = namespace["initialize_runtime"].__globals__
        runtime = self.root / "destination-runtime"
        values = {
            "RT": runtime,
            "STATE": runtime / "state.toml",
            "WORK": runtime / "work",
            "PASSED": runtime / "passed",
            "ARCHIVE": runtime / "archive",
        }
        globals_.update(values)
        globals_["CAT"] = SimpleNamespace(
            revision=DESTINATION_REVISION,
            by_id={stable_id: object() for stable_id in DESTINATION_ROUTE},
        )
        globals_["ROUTE"] = DESTINATION_ROUTE
        globals_["COMPLETION_DEPENDENCIES"] = DESTINATION_CONTRACT.prerequisites
        globals_["BASELINE_PROVIDERS"] = DESTINATION_CONTRACT.baseline_providers
        globals_["STORE"] = Mock()
        migration = Mock(return_value=True)
        globals_["migrate_core_route"] = migration

        globals_["initialize_runtime"]()

        migration.assert_called_once_with(
            values["STATE"],
            values["WORK"],
            values["ARCHIVE"],
            DESTINATION_CONTRACT,
        )
        globals_["STORE"].migrate_v2.assert_called_once_with(values["ARCHIVE"])

    def test_unknown_revision_fails_without_state_workspace_or_archive_mutation(self) -> None:
        original = dump_state(source_state()).replace(
            SOURCE_REVISION, "unknown-route", 1,
        ).encode()
        self.state_path.write_bytes(original)
        workspace = self.write_workspace("proc-read")
        workspace_bytes = (workspace / "source.c").read_bytes()

        with self.assertRaisesRegex(MigrationError, "no route migration"):
            self.migrate()

        self.assertEqual(self.state_path.read_bytes(), original)
        self.assertEqual((workspace / "source.c").read_bytes(), workspace_bytes)
        self.assertEqual(list(self.archive_root.iterdir()), [])
        self.assertFalse(self.state_path.with_name("state.toml.lock").exists())

    def test_active_contract_mismatch_fails_before_inspecting_or_mutating_state(self) -> None:
        original = b"not toml"
        self.state_path.write_bytes(original)
        wrong = CurriculumContract(
            DESTINATION_REVISION,
            DESTINATION_CONTRACT.ids,
            DESTINATION_CONTRACT.route,
            {
                **DESTINATION_CONTRACT.prerequisites,
                "module-build-lifecycle": frozenset({"precise-init-failure"}),
            },
            DESTINATION_CONTRACT.baseline_providers,
        )

        with self.assertRaisesRegex(MigrationError, "frozen core-route"):
            migrate_core_route(
                self.state_path, self.work_root, self.archive_root, wrong,
            )

        self.assertEqual(self.state_path.read_bytes(), original)
        self.assertEqual(list(self.archive_root.iterdir()), [])

    def test_symlinked_workspace_is_rejected_before_any_archive_is_written(self) -> None:
        original = dump_state(source_state()).encode()
        self.state_path.write_bytes(original)
        target = self.root / "outside"
        target.mkdir()
        (target / "source.c").write_text("outside\n")
        (self.work_root / "proc-read").symlink_to(target, target_is_directory=True)

        with self.assertRaisesRegex(MigrationError, "symlink"):
            self.migrate()

        self.assertEqual(self.state_path.read_bytes(), original)
        self.assertTrue((self.work_root / "proc-read").is_symlink())
        self.assertEqual(list(self.archive_root.iterdir()), [])

    def test_state_archive_collision_preserves_state_and_workspace(self) -> None:
        original = dump_state(source_state()).encode()
        self.state_path.write_bytes(original)
        workspace = self.write_workspace("proc-read")
        digest = hashlib.sha256(original).hexdigest()
        collision = self.archive_root / f"state-route-v2-{digest}.toml"
        collision.write_bytes(b"different bytes")

        with self.assertRaisesRegex(MigrationError, "does not match"):
            self.migrate()

        self.assertEqual(self.state_path.read_bytes(), original)
        self.assertTrue(workspace.exists())
        self.assertEqual(collision.read_bytes(), b"different bytes")

    def test_workspace_archive_collision_preserves_state_and_workspace(self) -> None:
        original = dump_state(source_state()).encode()
        self.state_path.write_bytes(original)
        workspace = self.write_workspace("proc-read")
        content_hash = canonical_tree_hash(workspace, ("source.c",))
        collision = self.archive_root / f"proc-read-{content_hash}"
        collision.mkdir()
        (collision / "different.c").write_text("collision\n")

        with self.assertRaisesRegex(MigrationError, "existing archive"):
            self.migrate()

        self.assertEqual(self.state_path.read_bytes(), original)
        self.assertTrue(workspace.exists())
        self.assertEqual((collision / "different.c").read_text(), "collision\n")

    def test_workspace_archival_failure_is_retryable(self) -> None:
        original = dump_state(source_state()).encode()
        self.state_path.write_bytes(original)
        workspace = self.write_workspace("proc-read")

        with patch(
            "engine.route_migration.archive_and_remove_workspaces",
            side_effect=WorkspaceError("injected archival failure"),
        ):
            with self.assertRaisesRegex(MigrationError, "injected archival failure"):
                self.migrate()

        self.assertEqual(self.state_path.read_bytes(), original)
        self.assertTrue(workspace.exists())
        self.assertTrue(any(
            path.name.startswith("state-route-v2-")
            for path in self.archive_root.iterdir()
        ))
        self.assertTrue(self.migrate())
        self.assertFalse(workspace.exists())
        self.assertEqual(
            parse_state(self.state_path.read_text()).curriculum_revision,
            DESTINATION_REVISION,
        )

    def test_state_replace_failure_after_workspace_removal_is_retryable(self) -> None:
        original = dump_state(source_state()).encode()
        self.state_path.write_bytes(original)
        workspace = self.write_workspace("proc-read")

        with patch(
            "engine.route_migration._atomic_replace",
            side_effect=OSError("injected replace failure"),
        ):
            with self.assertRaisesRegex(OSError, "injected replace failure"):
                self.migrate()

        self.assertEqual(self.state_path.read_bytes(), original)
        self.assertFalse(workspace.exists())
        self.assertTrue(any(
            path.name.startswith("proc-read-")
            for path in self.archive_root.iterdir()
        ))
        self.assertTrue(self.migrate())
        self.assertEqual(
            parse_state(self.state_path.read_text()).curriculum_revision,
            DESTINATION_REVISION,
        )


if __name__ == "__main__":
    unittest.main()
