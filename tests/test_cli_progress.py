from __future__ import annotations

from contextlib import contextmanager, redirect_stderr, redirect_stdout
import io
from pathlib import Path
import runpy
from types import SimpleNamespace
import tempfile
import unittest


ROOT = Path(__file__).resolve().parent.parent


class LearnerCLITests(unittest.TestCase):
    def setUp(self) -> None:
        namespace = runpy.run_path(ROOT / "rootkitlings", run_name="rootkitlings_test")
        self.globals = namespace["cmd_reset"].__globals__
        self.temporary = tempfile.TemporaryDirectory()
        runtime = Path(self.temporary.name) / ".rootkitlings"
        values = {
            "RT": runtime,
            "STATE": runtime / "state.toml",
            "WORK": runtime / "work",
            "PASSED": runtime / "passed",
            "ARCHIVE": runtime / "archive",
            "SETUP_STAMP": runtime / "setup.toml",
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
        self.globals["initialize_runtime"]()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _workspace(self, stable_id: str, content: str) -> Path:
        workspace = self.globals["WORK"] / stable_id
        workspace.mkdir()
        (workspace / "learner.c").write_text(content, encoding="utf-8")
        return workspace

    def _reset(self, arguments: list[str]) -> str:
        output = io.StringIO()
        with redirect_stdout(output):
            self.globals["cmd_reset"](arguments)
        return output.getvalue()

    def test_setup_accept_runs_doctor_then_module_free_probe_and_stamps(self) -> None:
        calls = []
        original_run = self.globals["run"]

        def fake_run(command, **kwargs):
            calls.append((command, kwargs))
            output = "[+] doctor passed\n" if len(calls) == 1 else "[+] PASSED\n"
            return SimpleNamespace(returncode=0, stdout=output)

        try:
            self.globals["run"] = fake_run
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                self.globals["cmd_setup"](["--accept"])
        finally:
            self.globals["run"] = original_run
        self.assertEqual(calls[0][0], [str(ROOT / "scripts/doctor")])
        self.assertEqual(calls[1][0], [
            str(ROOT / "vm/boot"),
            "--check",
            str(ROOT / "vm/checks/containment"),
        ])
        self.assertTrue(all(call[1]["capture"] for call in calls))
        self.assertTrue(self.globals["current_setup_status"]().ready)

    def test_setup_failure_never_writes_stamp(self) -> None:
        original_run = self.globals["run"]
        cases = (
            [SimpleNamespace(returncode=1, stdout="doctor failed\n")],
            [
                SimpleNamespace(returncode=0, stdout="doctor ok\n"),
                SimpleNamespace(returncode=0, stdout="missing marker\n"),
            ],
        )
        try:
            for results in cases:
                with self.subTest(results=len(results)):
                    if self.globals["SETUP_STAMP"].exists():
                        self.globals["SETUP_STAMP"].unlink()
                    queue = list(results)
                    self.globals["run"] = lambda *_args, **_kwargs: queue.pop(0)
                    with self.assertRaisesRegex(self.globals["CLIError"], "Setup failed"):
                        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                            self.globals["cmd_setup"](["--accept"])
                    self.assertFalse(self.globals["SETUP_STAMP"].exists())
        finally:
            self.globals["run"] = original_run

    def test_setup_without_accept_previews_policy_without_checks(self) -> None:
        original_run = self.globals["run"]
        try:
            self.globals["run"] = lambda *_args, **_kwargs: self.fail(
                "setup preview must not run checks"
            )
            output = io.StringIO()
            errors = io.StringIO()
            with redirect_stdout(output), redirect_stderr(errors):
                self.globals["cmd_setup"]([])
        finally:
            self.globals["run"] = original_run
        self.assertIn("Authorized disposable lab", output.getvalue())
        self.assertIn("Status: not completed", output.getvalue())
        self.assertIn("setup --accept", errors.getvalue())

    def test_setup_rejects_invalid_argument_shapes(self) -> None:
        for arguments in (["accept"], ["--accept", "extra"]):
            with self.subTest(arguments=arguments):
                with self.assertRaisesRegex(self.globals["CLIError"], r"setup \[--accept\]"):
                    self.globals["cmd_setup"](arguments)

    def test_setup_stale_reason_and_recovery_are_precise(self) -> None:
        expected = self.globals["expected_setup_stamp"]()
        stale = self.globals["SetupStamp"](
            policy_version=expected.policy_version,
            course_revision="old-revision",
            kernel_release=expected.kernel_release,
            boundary_hash=expected.boundary_hash,
        )
        self.globals["write_setup_stamp"](
            self.globals["SETUP_STAMP"],
            stale,
            checks_passed=True,
        )
        self.assertEqual(
            self.globals["current_setup_status"]().reason,
            "course-revision-changed",
        )
        with self.assertRaisesRegex(
            self.globals["CLIError"],
            r"course revision changed.*rootkitlings setup --accept",
        ):
            self.globals["require_setup"]()

    def test_main_gates_only_build_and_execution_commands(self) -> None:
        original_commands = self.globals["CMDS"]
        original_initialize = self.globals["initialize_runtime"]
        original_require = self.globals["require_setup"]
        gated = []
        commands = {
            name: (lambda _arguments: None)
            for name in (
                "setup", "read", "start", "build", "check", "hint",
                "solution", "run", "gdb", "status", "list", "prereq",
                "restore", "reset", "clean",
            )
        }
        try:
            self.globals["CMDS"] = commands
            self.globals["initialize_runtime"] = lambda: None
            self.globals["require_setup"] = lambda: gated.append(True)
            for command in commands:
                gated.clear()
                self.globals["main"]([command])
                self.assertEqual(
                    bool(gated),
                    command in {"start", "build", "check", "run", "gdb"},
                )
        finally:
            self.globals["CMDS"] = original_commands
            self.globals["initialize_runtime"] = original_initialize
            self.globals["require_setup"] = original_require

    def test_status_reports_setup_separately_from_course_count(self) -> None:
        output = io.StringIO()
        with redirect_stdout(output):
            self.globals["cmd_status"]([])
        lines = output.getvalue().splitlines()
        self.assertRegex(lines[0], r"^Setup   required \(not completed\)")
        self.assertRegex(lines[1], r"^Course  0/\d+ complete$")
        self.assertIn("Workspace not started", lines)
        self.assertIn("Next    ./rootkitlings start L00", lines)

    def test_help_uses_short_reset_and_documents_current_default(self) -> None:
        output = io.StringIO()
        with redirect_stdout(output):
            self.globals["usage"]()
        help_text = output.getvalue()
        self.assertIn("reset --all --yes", help_text)
        self.assertNotIn("progress-reset", help_text)
        self.assertIn("current exercise is used", help_text)

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
            self.globals["run_host_check"](exercise, workspace, artifact, 17)
        finally:
            self.globals["run"] = original_run
        command, kwargs = calls[0]
        self.assertEqual(command[-2:], [str(workspace), str(artifact)])
        self.assertEqual(kwargs, {"timeout": 17, "capture": True})

    def test_check_runs_each_declarative_case_and_records_one_attempt(self) -> None:
        exercise = self.globals["CAT"].ordered()[0]
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            self.globals["cmd_start"]([exercise.serial])
        build_root = self.globals["WORK"] / exercise.id
        artifact = build_root / self.globals["CAT"].module_artifact(exercise)
        artifact.write_bytes(b"module")
        success = SimpleNamespace(
            id="normal",
            label="Normal load",
            check=exercise.resources.check,
        )
        rejection = SimpleNamespace(
            id="invalid",
            label="Invalid build setting",
            check="",
        )
        outcomes = (
            SimpleNamespace(
                case=success,
                build_root=build_root,
                artifact=artifact,
                output="",
            ),
            SimpleNamespace(
                case=rejection,
                build_root=build_root,
                artifact=None,
                output="rejected",
            ),
        )

        @contextmanager
        def fake_builds(*_args, **_kwargs):
            source_hash = self.globals["canonical_tree_hash"](
                build_root,
                exercise.workspace.files,
            )
            yield SimpleNamespace(
                outcomes=outcomes,
                frozen_source=build_root,
                source_hash=source_hash,
            )

        originals = {
            name: self.globals[name]
            for name in (
                "build_verification_cases",
                "run",
                "run_host_check",
                "record_attempt",
            )
        }
        attempts = []
        guest_runs = []
        try:
            self.globals["build_verification_cases"] = fake_builds
            self.globals["run_host_check"] = (
                lambda *_args: SimpleNamespace(returncode=0, stdout="")
            )
            self.globals["run"] = (
                lambda command, **_kwargs: guest_runs.append(command)
                or SimpleNamespace(returncode=0, stdout="[+] PASSED\n")
            )
            self.globals["record_attempt"] = (
                lambda item, passed, *_args: attempts.append((item.id, passed)) or False
            )
            output = io.StringIO()
            with redirect_stdout(output), redirect_stderr(io.StringIO()):
                self.globals["cmd_check"]([exercise.serial])
        finally:
            self.globals.update(originals)
        self.assertIn("Check 1/2: Normal load", output.getvalue())
        self.assertIn("Check 2/2: Invalid build setting", output.getvalue())
        self.assertIn("Build rejected as required", output.getvalue())
        self.assertEqual(len(guest_runs), 1)
        self.assertEqual(attempts, [(exercise.id, True)])

    def test_missing_workspace_is_not_an_attempt(self) -> None:
        exercise = self.globals["CAT"].ordered()[0]
        with self.assertRaisesRegex(self.globals["CLIError"], "has no workspace"):
            self.globals["cmd_check"]([exercise.serial])
        with self.globals["STORE"].read_locked() as state:
            self.assertNotIn(exercise.id, state.exercises)

    def test_changed_workspace_records_one_failed_attempt_not_pass(self) -> None:
        exercise = self.globals["CAT"].ordered()[0]
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            self.globals["cmd_start"]([exercise.serial])
        workspace = self.globals["WORK"] / exercise.id
        frozen_parent = Path(self.temporary.name) / "frozen"
        frozen_parent.mkdir()
        frozen = self.globals["materialize"](
            workspace,
            frozen_parent,
            "source",
            exercise.workspace.files,
        )
        verified_hash = self.globals["canonical_tree_hash"](
            frozen,
            exercise.workspace.files,
        )
        editable = workspace / exercise.workspace.editable[0]
        editable.write_text(editable.read_text() + "\n/* changed */\n")

        changed = self.globals["record_attempt"](
            exercise,
            True,
            frozen,
            verified_hash,
        )
        self.assertTrue(changed)
        with self.globals["STORE"].read_locked() as state:
            record = state.exercises[exercise.id]
            self.assertEqual(record.attempts, 1)
            self.assertNotIn(exercise.id, state.completed)
            self.assertEqual(record.passed_snapshot, "")

    def test_guest_launch_error_records_exactly_one_attempt(self) -> None:
        exercise = self.globals["CAT"].ordered()[0]
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            self.globals["cmd_start"]([exercise.serial])
        workspace = self.globals["WORK"] / exercise.id
        artifact = workspace / self.globals["CAT"].module_artifact(exercise)
        artifact.write_bytes(b"module")
        outcome = SimpleNamespace(
            case=SimpleNamespace(label="Control", check=exercise.resources.check),
            build_root=workspace,
            artifact=artifact,
        )

        @contextmanager
        def fake_builds(*_args, **_kwargs):
            yield SimpleNamespace(
                outcomes=(outcome,),
                frozen_source=workspace,
                source_hash=self.globals["canonical_tree_hash"](
                    workspace,
                    exercise.workspace.files,
                ),
            )

        original_builds = self.globals["build_verification_cases"]
        original_run = self.globals["run"]
        try:
            self.globals["build_verification_cases"] = fake_builds
            self.globals["run"] = lambda *_args, **_kwargs: self.globals["die"](
                "cannot execute guest"
            )
            with self.assertRaisesRegex(self.globals["CLIError"], "cannot execute"):
                with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                    self.globals["cmd_check"]([exercise.serial])
        finally:
            self.globals["build_verification_cases"] = original_builds
            self.globals["run"] = original_run
        with self.globals["STORE"].read_locked() as state:
            self.assertEqual(state.exercises[exercise.id].attempts, 1)

    def test_template_solution_renders_recursive_paths_in_order(self) -> None:
        exercise = SimpleNamespace(
            id="template",
            serial="L00",
            title="Template lesson",
            workspace=SimpleNamespace(baseline="template"),
        )
        tree = {
            "status/main.c": SimpleNamespace(data=b"status\n"),
            "core/main.c": SimpleNamespace(data=b"core\n"),
            "Kbuild": SimpleNamespace(data=b"build\n"),
        }
        original_catalog = self.globals["CAT"]
        try:
            self.globals["CAT"] = SimpleNamespace(
                solution_tree=lambda _exercise: tree,
                baseline_provider=lambda _exercise: None,
            )
            output = self.globals["solution_output"](exercise, set())
        finally:
            self.globals["CAT"] = original_catalog
        self.assertIn("Complete reference files", output)
        self.assertIn("Viewing this reference is recorded", output)
        headings = (
            "--- Kbuild ---",
            "--- core/main.c ---",
            "--- status/main.c ---",
        )
        positions = [output.index(value) for value in headings]
        self.assertEqual(positions, sorted(positions))
        self.assertNotIn("--- main.c ---", output)

    def test_derived_solution_shows_only_delta_and_provider(self) -> None:
        provider = SimpleNamespace(
            id="provider",
            serial="L00",
            title="Provider lesson",
        )
        exercise = SimpleNamespace(
            id="consumer",
            serial="L01",
            title="Consumer lesson",
            workspace=SimpleNamespace(baseline="previous-required"),
        )
        provider_tree = {
            "Kbuild": SimpleNamespace(data=b"unchanged sentinel\n"),
            "core/main.c": SimpleNamespace(data=b"old\n"),
        }
        consumer_tree = {
            "Kbuild": SimpleNamespace(data=b"unchanged sentinel\n"),
            "core/main.c": SimpleNamespace(data=b"new\n"),
            "include/api.h": SimpleNamespace(data=b"api\n"),
        }
        original_catalog = self.globals["CAT"]
        try:
            self.globals["CAT"] = SimpleNamespace(
                solution_tree=lambda item: (
                    provider_tree if item is provider else consumer_tree
                ),
                baseline_provider=lambda item: (
                    provider if item is exercise else None
                ),
            )
            output = self.globals["solution_output"](exercise, set())
            completed = self.globals["solution_output"](
                exercise,
                {provider.id},
            )
        finally:
            self.globals["CAT"] = original_catalog
        self.assertIn("Compared with L00: Provider lesson", output)
        self.assertIn("not complete", output)
        self.assertNotIn("not complete", completed)
        self.assertNotIn("unchanged sentinel", output)
        self.assertIn("--- a/core/main.c", output)
        self.assertIn("+++ b/core/main.c", output)
        self.assertIn("--- /dev/null", output)
        self.assertIn("+++ b/include/api.h", output)

    def test_explicit_completion_solution_remains_complete(self) -> None:
        provider = SimpleNamespace(id="provider", serial="L00", title="Provider")
        exercise = SimpleNamespace(
            id="completion",
            serial="L01",
            title="Completion boundary",
            workspace=SimpleNamespace(baseline="completion"),
        )
        tree = {"main.c": SimpleNamespace(data=b"complete\n")}
        original_catalog = self.globals["CAT"]
        try:
            self.globals["CAT"] = SimpleNamespace(
                solution_tree=lambda _exercise: tree,
                baseline_provider=lambda _exercise: provider,
            )
            output = self.globals["solution_output"](exercise, set())
        finally:
            self.globals["CAT"] = original_catalog
        self.assertIn("Complete reference files", output)
        self.assertIn("--- main.c ---", output)
        self.assertNotIn("--- a/main.c", output)

    def test_solution_disclosure_records_only_after_render(self) -> None:
        exercise = self.globals["CAT"].ordered()[0]
        original_output = self.globals["solution_output"]
        try:
            self.globals["solution_output"] = lambda *_args: self.globals["die"](
                "composition failed"
            )
            with self.assertRaisesRegex(
                self.globals["CLIError"],
                "composition failed",
            ):
                self.globals["cmd_solution"]([exercise.serial])
            with self.globals["STORE"].read_locked() as state:
                self.assertNotIn(exercise.id, state.exercises)

            future = self.globals["CAT"].ordered()[-1]
            self.globals["solution_output"] = lambda *_args: "rendered\n"
            with redirect_stdout(io.StringIO()):
                self.globals["cmd_solution"]([future.serial])
            with self.globals["STORE"].read_locked() as state:
                self.assertTrue(state.exercises[future.id].solution_revealed)
        finally:
            self.globals["solution_output"] = original_output

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

        output = self._reset([selected])

        with self.globals["STORE"].read_locked() as state:
            self.assertEqual(state.completed, {earlier})
            self.assertEqual(set(state.exercises), {earlier})
        self.assertTrue(earlier_workspace.exists())
        self.assertFalse(selected_workspace.exists())
        self.assertFalse(later_workspace.exists())
        self.assertTrue(marker.exists())
        archives = list(self.globals["ARCHIVE"].iterdir())
        self.assertEqual(len(archives), 2)
        self.assertIn("Reset progress from L01 onward", output)

    def test_all_reset_is_safe_when_workspaces_are_absent(self) -> None:
        first = self.globals["ROUTE"][0]
        with self.globals["STORE"].locked() as state:
            state.completed.add(first)
            state.exercise(first).attempts = 1

        with self.assertRaisesRegex(self.globals["CLIError"], "--all --yes"):
            self._reset(["--all"])

        output = self._reset(["--all", "--yes"])

        with self.globals["STORE"].read_locked() as state:
            self.assertEqual(state.completed, set())
            self.assertEqual(state.exercises, {})
        self.assertIn("Reset all course progress", output)

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
