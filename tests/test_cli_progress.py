from __future__ import annotations

from contextlib import redirect_stdout
import io
from pathlib import Path
import runpy
from types import SimpleNamespace
import tempfile
import unittest


ROOT = Path(__file__).resolve().parent.parent


class ProgressResetCLITests(unittest.TestCase):
    def setUp(self) -> None:
        namespace = runpy.run_path(ROOT / "rootkitlings", run_name="rootkitlings_test")
        self.globals = namespace["cmd_progress_reset"].__globals__
        self.temporary = tempfile.TemporaryDirectory()
        runtime = Path(self.temporary.name) / ".rootkitlings"
        values = {
            "RT": runtime,
            "STATE": runtime / "state.toml",
            "WORK": runtime / "work",
            "PASSED": runtime / "passed",
            "ARCHIVE": runtime / "archive",
        }
        self.globals.update(values)
        self.globals["STORE"] = self.globals["StateStore"](
            values["STATE"],
            set(self.globals["CAT"].by_id),
            self.globals["ROUTE"],
            self.globals["CAT"].revision,
            self.globals["COMPLETION_DEPENDENCIES"],
            self.globals["BASELINE_PROVIDERS"],
        )
        self.globals["setup"]()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _workspace(self, stable_id: str, content: str) -> Path:
        workspace = self.globals["WORK"] / stable_id
        workspace.mkdir()
        (workspace / "learner.c").write_text(content, encoding="utf-8")
        return workspace

    def _progress_reset(self, arguments: list[str]) -> str:
        output = io.StringIO()
        with redirect_stdout(output):
            self.globals["cmd_progress_reset"](arguments)
        return output.getvalue()

    def _build_exercise(self, stable_id="artifact-test", host_check=""):
        return SimpleNamespace(
            id=stable_id,
            serial="L99",
            workspace=SimpleNamespace(build=".", module_artifact="hello.ko"),
            resources=SimpleNamespace(host_check=host_check),
        )

    def test_build_uses_override_and_removes_exact_stale_artifact(self) -> None:
        exercise = self._build_exercise()
        workspace = self.globals["WORK"] / exercise.id
        workspace.mkdir()
        artifact = workspace / "rootkitlings.ko"
        artifact.write_bytes(b"stale")
        original_catalog = self.globals["CAT"]
        original_run = self.globals["run"]
        observed = []

        def fake_run(_command, **_kwargs):
            observed.append(not artifact.exists())
            artifact.write_bytes(b"fresh")
            return SimpleNamespace(returncode=0)

        try:
            self.globals["CAT"] = SimpleNamespace(
                module_artifact=lambda _exercise: "rootkitlings.ko"
            )
            self.globals["run"] = fake_run
            result = self.globals["build_workspace"](exercise)
        finally:
            self.globals["CAT"] = original_catalog
            self.globals["run"] = original_run
        self.assertEqual(result, artifact)
        self.assertEqual(artifact.read_bytes(), b"fresh")
        self.assertEqual(observed, [True])

    def test_failed_build_cannot_reuse_stale_artifact(self) -> None:
        exercise = self._build_exercise()
        workspace = self.globals["WORK"] / exercise.id
        workspace.mkdir()
        artifact = workspace / "hello.ko"
        artifact.write_bytes(b"stale")
        original_run = self.globals["run"]
        try:
            self.globals["run"] = lambda _command, **_kwargs: SimpleNamespace(
                returncode=1
            )
            with self.assertRaisesRegex(self.globals["CLIError"], "Build failed"):
                self.globals["build_workspace"](exercise)
        finally:
            self.globals["run"] = original_run
        self.assertFalse(artifact.exists())

    def test_build_rejects_symlink_and_special_artifact_paths(self) -> None:
        exercise = self._build_exercise()
        workspace = self.globals["WORK"] / exercise.id
        workspace.mkdir()
        artifact = workspace / "hello.ko"
        target = workspace / "outside"
        target.write_bytes(b"outside")
        artifact.symlink_to(target)
        with self.assertRaisesRegex(self.globals["CLIError"], "symlink"):
            self.globals["build_workspace"](exercise)
        artifact.unlink()
        artifact.mkdir()
        with self.assertRaisesRegex(self.globals["CLIError"], "regular file"):
            self.globals["build_workspace"](exercise)

    def test_host_check_receives_build_and_exact_artifact_abi(self) -> None:
        exercise = self._build_exercise(host_check="support/host-check")
        workspace = self.globals["WORK"] / exercise.id
        workspace.mkdir()
        artifact = workspace / "rootkitlings.ko"
        artifact.write_bytes(b"module")
        original_run = self.globals["run"]
        calls = []
        try:
            self.globals["run"] = (
                lambda command, **kwargs: calls.append((command, kwargs))
                or SimpleNamespace(returncode=0)
            )
            self.globals["run_host_check"](exercise, artifact)
        finally:
            self.globals["run"] = original_run
        command, kwargs = calls[0]
        self.assertEqual(command[-2:], [str(workspace), str(artifact)])
        self.assertEqual(kwargs, {})

    def test_start_records_template_and_inherited_baseline_provenance(self) -> None:
        first, second = (
            self.globals["CAT"].by_id[stable_id]
            for stable_id in self.globals["ROUTE"][:2]
        )
        with redirect_stdout(io.StringIO()):
            self.globals["cmd_start"]([first.serial])
        with self.globals["STORE"].locked() as state:
            first_record = state.exercises[first.id]
            self.assertEqual(first_record.baseline_provider, "")
            state.completed.add(first.id)
            first_record.passed_snapshot = first_record.baseline_hash
        with redirect_stdout(io.StringIO()):
            self.globals["cmd_start"]([second.serial])
        with self.globals["STORE"].read_locked() as state:
            self.assertEqual(
                state.exercises[second.id].baseline_provider,
                first.id,
            )
        self.globals["record_attempt"](second, False)
        with self.globals["STORE"].read_locked() as state:
            self.assertEqual(
                state.exercises[second.id].baseline_provider,
                first.id,
            )

    def test_restore_preserves_baseline_provider(self) -> None:
        first, second = (
            self.globals["CAT"].by_id[stable_id]
            for stable_id in self.globals["ROUTE"][:2]
        )
        with redirect_stdout(io.StringIO()):
            self.globals["cmd_start"]([first.serial])
        with self.globals["STORE"].locked() as state:
            record = state.exercises[first.id]
            state.completed.add(first.id)
            record.passed_snapshot = record.baseline_hash
        with redirect_stdout(io.StringIO()):
            self.globals["cmd_start"]([second.serial])
            self.globals["cmd_restore"]([second.serial])
        with self.globals["STORE"].read_locked() as state:
            self.assertEqual(
                state.exercises[second.id].baseline_provider,
                first.id,
            )

    def test_required_reset_archives_and_clears_suffix_only(self) -> None:
        route = self.globals["ROUTE"]
        earlier, selected, later = route[:3]
        with self.globals["STORE"].locked() as state:
            state.completed.update((earlier, selected, later))
            for stable_id in (earlier, selected, later):
                state.exercise(stable_id).attempts = 2

        earlier_workspace = self._workspace(earlier, "keep\n")
        selected_workspace = self._workspace(selected, "archive selected\n")
        later_workspace = self._workspace(later, "archive later\n")
        marker = self.globals["PASSED"] / "untouched"
        marker.write_text("snapshot", encoding="utf-8")

        output = self._progress_reset([selected])

        with self.globals["STORE"].read_locked() as state:
            self.assertEqual(state.completed, {earlier})
            self.assertEqual(set(state.exercises), {earlier})
        self.assertTrue(earlier_workspace.exists())
        self.assertFalse(selected_workspace.exists())
        self.assertFalse(later_workspace.exists())
        self.assertTrue(marker.exists())
        archives = list(self.globals["ARCHIVE"].iterdir())
        self.assertEqual(len(archives), 2)
        self.assertIn("Reset L01 and its dependents", output)

    def test_all_reset_is_safe_when_workspaces_are_absent(self) -> None:
        first = self.globals["ROUTE"][0]
        with self.globals["STORE"].locked() as state:
            state.completed.add(first)
            state.exercise(first).attempts = 1

        output = self._progress_reset(["--all"])

        with self.globals["STORE"].read_locked() as state:
            self.assertEqual(state.completed, set())
            self.assertEqual(state.exercises, {})
        self.assertIn("Reset all learner progress", output)

    def test_elective_reset_includes_only_elective_descendants(self) -> None:
        workspace = SimpleNamespace(parent="")
        required = SimpleNamespace(
            id="required",
            status="required",
            prerequisites=(),
            workspace=workspace,
        )
        elective = SimpleNamespace(
            id="elective",
            status="elective",
            prerequisites=("required",),
            workspace=workspace,
        )
        descendant = SimpleNamespace(
            id="descendant",
            status="elective",
            prerequisites=("elective",),
            workspace=workspace,
        )
        unrelated = SimpleNamespace(
            id="unrelated",
            status="elective",
            prerequisites=("required",),
            workspace=workspace,
        )
        original_catalog = self.globals["CAT"]
        original_route = self.globals["ROUTE"]
        try:
            self.globals["CAT"] = SimpleNamespace(
                by_id={item.id: item for item in (required, elective, descendant, unrelated)},
                exercises=(required, elective, descendant, unrelated),
                completion_dependencies=lambda item: item.prerequisites,
            )
            self.globals["ROUTE"] = ("required",)
            affected = self.globals["progress_reset_ids"](elective)
        finally:
            self.globals["CAT"] = original_catalog
            self.globals["ROUTE"] = original_route
        self.assertEqual(affected, {"elective", "descendant"})

    def test_derived_baseline_requires_provider_snapshot(self) -> None:
        provider = SimpleNamespace(id="provider", serial="L00")
        exercise = SimpleNamespace(id="consumer", serial="L01")
        state = SimpleNamespace(exercises={})
        original_catalog = self.globals["CAT"]
        try:
            self.globals["CAT"] = SimpleNamespace(
                baseline_provider=lambda _exercise: provider
            )
            with self.assertRaisesRegex(
                self.globals["CLIError"],
                "needs a passed snapshot from L00",
            ):
                self.globals["baseline"](exercise, state)
        finally:
            self.globals["CAT"] = original_catalog

    def test_additive_baseline_uses_catalog_provider_and_additions(self) -> None:
        provider = SimpleNamespace(
            id="provider",
            serial="L00",
            workspace=SimpleNamespace(files=("main.c",)),
        )
        exercise = SimpleNamespace(
            id="consumer",
            workspace=SimpleNamespace(
                files=("main.c", "status.c"),
                additions="curriculum/additions/consumer",
            ),
        )
        state = SimpleNamespace(
            exercises={"provider": SimpleNamespace(passed_snapshot="a" * 64)}
        )
        calls = []
        original_catalog = self.globals["CAT"]
        original_snapshot = self.globals["snap"]
        original_materialize = self.globals["materialize_inherited"]
        try:
            self.globals["CAT"] = SimpleNamespace(
                baseline_provider=lambda _exercise: provider
            )
            self.globals["snap"] = lambda _hash: Path("/verified/snapshot")
            self.globals["materialize_inherited"] = (
                lambda *arguments: calls.append(arguments) or Path("/workspace")
            )
            result = self.globals["materialize_workspace"](exercise, state)
        finally:
            self.globals["CAT"] = original_catalog
            self.globals["snap"] = original_snapshot
            self.globals["materialize_inherited"] = original_materialize
        self.assertEqual(result, Path("/workspace"))
        self.assertEqual(calls[0][0], Path("/verified/snapshot"))
        self.assertEqual(calls[0][1], ROOT / "curriculum/additions/consumer")
        self.assertEqual(calls[0][4], ("main.c",))
        self.assertEqual(calls[0][5], ("main.c", "status.c"))


if __name__ == "__main__":
    unittest.main()
