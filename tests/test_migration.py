from __future__ import annotations

import os
from pathlib import Path
import tempfile
import unittest

from engine.migration import (
    CompletionCredit,
    CurriculumContract,
    HistoryMerge,
    MigrationError,
    MigrationPlan,
    SnapshotCompatibility,
    apply_migration,
    migrate_state_file,
)
from engine.state import ExerciseState, LearnerState, dump_state, parse_state


HASH_A = "a" * 64
HASH_B = "b" * 64
SOURCE = CurriculumContract(
    "old",
    frozenset({"old-a", "old-b", "removed"}),
    ("old-a", "old-b", "removed"),
    {
        "old-a": frozenset(),
        "old-b": frozenset({"old-a"}),
        "removed": frozenset({"old-b"}),
    },
)
DESTINATION = CurriculumContract(
    "new",
    frozenset({"foundation", "core", "optional"}),
    ("foundation", "core"),
    {
        "foundation": frozenset(),
        "core": frozenset({"foundation"}),
        "optional": frozenset({"foundation"}),
    },
)


def source_state() -> LearnerState:
    return LearnerState(
        "old",
        SOURCE.route,
        {"old-a", "old-b"},
        {
            "old-a": ExerciseState(HASH_A, HASH_A, HASH_A, 2, False),
            "old-b": ExerciseState(HASH_B, HASH_B, HASH_B, 3, True),
            "removed": ExerciseState(attempts=7),
        },
    )


def merge_plan(compatible: bool = False) -> MigrationPlan:
    return MigrationPlan(
        "old",
        "new",
        {"old-a": "foundation"},
        frozenset({"old-b", "removed"}),
        (CompletionCredit("core", ("old-a", "old-b")),),
        (HistoryMerge("core", ("old-b",)),),
        (
            (SnapshotCompatibility("old-a", "foundation"),)
            if compatible
            else ()
        ),
    )


class MigrationTests(unittest.TestCase):
    def test_unchanged_id_preserves_completion_and_history(self) -> None:
        source_contract = CurriculumContract(
            "old",
            frozenset({"kept"}),
            ("kept",),
            {"kept": frozenset()},
        )
        destination_contract = CurriculumContract(
            "new",
            frozenset({"kept"}),
            ("kept",),
            {"kept": frozenset()},
        )
        source = LearnerState(
            "old",
            ("kept",),
            {"kept"},
            {"kept": ExerciseState(HASH_A, HASH_A, HASH_A, 4, True)},
        )

        migrated = apply_migration(
            source,
            MigrationPlan("old", "new", {}, frozenset()),
            source_contract,
            destination_contract,
        )

        self.assertEqual(migrated.completed, {"kept"})
        self.assertEqual(migrated.exercises["kept"].attempts, 4)
        self.assertTrue(migrated.exercises["kept"].solution_revealed)
        self.assertEqual(migrated.exercises["kept"].passed_snapshot, "")

    def test_credit_and_history_merge_are_independent_and_deterministic(self) -> None:
        first = apply_migration(source_state(), merge_plan(), SOURCE, DESTINATION)
        second = apply_migration(source_state(), merge_plan(), SOURCE, DESTINATION)
        self.assertEqual(dump_state(first), dump_state(second))
        self.assertEqual(first.completed, {"foundation", "core"})
        self.assertEqual(first.exercises["foundation"].attempts, 2)
        self.assertEqual(first.exercises["core"].attempts, 3)
        self.assertTrue(first.exercises["core"].solution_revealed)
        self.assertEqual(first.exercises["core"].passed_snapshot, "")

        partial = source_state()
        partial.completed.remove("old-b")
        result = apply_migration(partial, merge_plan(), SOURCE, DESTINATION)
        self.assertEqual(result.completed, {"foundation"})
        self.assertEqual(result.exercises["core"].attempts, 3)

    def test_history_source_cannot_feed_two_destinations(self) -> None:
        duplicate = MigrationPlan(
            "old",
            "new",
            {"old-a": "foundation"},
            frozenset({"old-b", "removed"}),
            (CompletionCredit("core", ("old-a", "old-b")),),
            (HistoryMerge("core", ("old-a", "old-b")),),
        )
        with self.assertRaisesRegex(MigrationError, "no direct destination"):
            apply_migration(source_state(), duplicate, SOURCE, DESTINATION)

        repeated = MigrationPlan(
            "old",
            "new",
            {"old-a": "foundation"},
            frozenset({"old-b", "removed"}),
            (CompletionCredit("core", ("old-a", "old-b")),),
            (
                HistoryMerge("core", ("old-b",)),
                HistoryMerge("optional", ("old-b",)),
            ),
        )
        with self.assertRaisesRegex(MigrationError, "only one history merge"):
            apply_migration(source_state(), repeated, SOURCE, DESTINATION)

    def test_credit_must_include_all_direct_mapped_sources(self) -> None:
        ambiguous = MigrationPlan(
            "old",
            "new",
            {"old-a": "foundation"},
            frozenset({"old-b", "removed"}),
            (CompletionCredit("foundation", ("old-b",)),),
            (HistoryMerge("core", ("old-b",)),),
        )
        with self.assertRaisesRegex(MigrationError, "every direct-mapped source"):
            apply_migration(source_state(), ambiguous, SOURCE, DESTINATION)

    def test_source_and_destination_contracts_are_fully_validated(self) -> None:
        bad_source = CurriculumContract(
            SOURCE.revision,
            SOURCE.ids,
            SOURCE.route,
            {**SOURCE.prerequisites, "old-a": frozenset({"old-b"})},
        )
        with self.assertRaisesRegex(MigrationError, "not earlier"):
            apply_migration(source_state(), merge_plan(), bad_source, DESTINATION)

        bad_destination = CurriculumContract(
            DESTINATION.revision,
            DESTINATION.ids,
            DESTINATION.route,
            {**DESTINATION.prerequisites, "optional": frozenset({"optional"})},
        )
        with self.assertRaisesRegex(MigrationError, "self prerequisite"):
            apply_migration(source_state(), merge_plan(), SOURCE, bad_destination)

    def test_source_state_must_pass_its_contract(self) -> None:
        invalid = source_state()
        invalid.completed = {"old-b"}
        with self.assertRaisesRegex(MigrationError, "source state violates"):
            apply_migration(invalid, merge_plan(), SOURCE, DESTINATION)

    def test_final_state_must_pass_destination_validation(self) -> None:
        incomplete_plan = MigrationPlan(
            "old",
            "new",
            {"old-b": "core"},
            frozenset({"old-a", "removed"}),
        )
        with self.assertRaisesRegex(MigrationError, "destination contract"):
            apply_migration(source_state(), incomplete_plan, SOURCE, DESTINATION)

    def test_dead_plan_sources_and_implicit_many_to_one_renames_are_rejected(self) -> None:
        typo = MigrationPlan(
            "old",
            "new",
            {"old-a": "foundation"},
            frozenset({"old-b", "removed", "old-typo"}),
        )
        with self.assertRaisesRegex(MigrationError, "unknown source IDs"):
            apply_migration(source_state(), typo, SOURCE, DESTINATION)

        implicit_merge = MigrationPlan(
            "old",
            "new",
            {"old-a": "foundation", "old-b": "foundation"},
            frozenset({"removed"}),
        )
        with self.assertRaisesRegex(MigrationError, "one-to-one"):
            apply_migration(source_state(), implicit_merge, SOURCE, DESTINATION)

    def test_hash_retention_requires_declaration_and_successful_verification(self) -> None:
        cleared = apply_migration(source_state(), merge_plan(), SOURCE, DESTINATION)
        self.assertEqual(cleared.exercises["foundation"].baseline_hash, "")

        with self.assertRaisesRegex(MigrationError, "explicit verifier"):
            apply_migration(
                source_state(),
                merge_plan(compatible=True),
                SOURCE,
                DESTINATION,
            )
        with self.assertRaisesRegex(MigrationError, "verification failed"):
            apply_migration(
                source_state(),
                merge_plan(compatible=True),
                SOURCE,
                DESTINATION,
                lambda _source, _destination, _record: False,
            )
        verified = apply_migration(
            source_state(),
            merge_plan(compatible=True),
            SOURCE,
            DESTINATION,
            lambda source, destination, record: (
                source == "old-a"
                and destination == "foundation"
                and record.passed_snapshot == HASH_A
            ),
        )
        self.assertEqual(verified.exercises["foundation"].passed_snapshot, HASH_A)

    def test_compatible_snapshot_records_destination_provider_provenance(self) -> None:
        source_contract = CurriculumContract(
            "old",
            frozenset({"base", "child"}),
            ("base", "child"),
            {"base": frozenset(), "child": frozenset({"base"})},
            {"base": None, "child": "base"},
        )
        destination_contract = CurriculumContract(
            "new",
            frozenset({"base", "child"}),
            ("base", "child"),
            {"base": frozenset(), "child": frozenset({"base"})},
            {"base": None, "child": "base"},
        )
        source = LearnerState(
            "old",
            ("base", "child"),
            {"base", "child"},
            {
                "base": ExerciseState(HASH_A, HASH_A, HASH_A),
                "child": ExerciseState(
                    HASH_B, HASH_B, HASH_B,
                    baseline_provider="base",
                ),
            },
        )
        plan = MigrationPlan(
            "old",
            "new",
            {},
            frozenset(),
            compatible_snapshots=(SnapshotCompatibility("child", "child"),),
        )
        migrated = apply_migration(
            source,
            plan,
            source_contract,
            destination_contract,
            lambda *_arguments: True,
        )
        self.assertEqual(migrated.exercises["child"].baseline_provider, "base")

    def test_file_migration_archives_exact_bytes_before_replacement(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state_path = root / "state.toml"
            archive_root = root / "archive"
            original = dump_state(source_state()).encode()
            state_path.write_bytes(original)
            archive = migrate_state_file(
                state_path,
                archive_root,
                merge_plan(),
                SOURCE,
                DESTINATION,
            )
            self.assertEqual(archive.read_bytes(), original)
            migrated = parse_state(state_path.read_text())
            self.assertEqual(migrated.curriculum_revision, "new")

    def test_every_precommit_failure_preserves_exact_source_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state_path = root / "state.toml"
            archive_root = root / "archive"
            original = dump_state(source_state()).encode()
            state_path.write_bytes(original)
            with self.assertRaisesRegex(MigrationError, "explicit verifier"):
                migrate_state_file(
                    state_path,
                    archive_root,
                    merge_plan(compatible=True),
                    SOURCE,
                    DESTINATION,
                )
            self.assertEqual(state_path.read_bytes(), original)
            self.assertEqual(list(archive_root.iterdir()), [])

    def test_filesystem_boundary_rejects_shared_or_symlinked_parent(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shared = root / "shared"
            shared.mkdir(mode=0o777)
            os.chmod(shared, 0o777)
            state_path = shared / "state.toml"
            state_path.write_text(dump_state(source_state()))
            with self.assertRaisesRegex(MigrationError, "private"):
                migrate_state_file(
                    state_path,
                    shared / "archive",
                    merge_plan(),
                    SOURCE,
                    DESTINATION,
                )

            real = root / "real"
            real.mkdir()
            linked = root / "linked"
            linked.symlink_to(real, target_is_directory=True)
            linked_state = linked / "state.toml"
            (real / "state.toml").write_text(dump_state(source_state()))
            with self.assertRaisesRegex(MigrationError, "non-symlinked"):
                migrate_state_file(
                    linked_state,
                    real / "archive",
                    merge_plan(),
                    SOURCE,
                    DESTINATION,
                )


if __name__ == "__main__":
    unittest.main()
