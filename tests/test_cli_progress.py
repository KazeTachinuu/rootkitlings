from __future__ import annotations

from contextlib import contextmanager, redirect_stderr, redirect_stdout
import io
import os
from pathlib import Path
import runpy
from types import SimpleNamespace
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parent.parent


class LearnerCLITests(unittest.TestCase):
    def setUp(self) -> None:
        namespace = runpy.run_path(
            ROOT / "engine/cli.py",
            run_name="rootkitlings_test",
        )
        self.globals = namespace["cmd_reset"].__globals__
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

    def test_setup_prepares_kernel_then_probes_guest(self) -> None:
        calls = []
        original_run = self.globals["run"]

        def fake_run(command, **kwargs):
            calls.append((command, kwargs))
            if command == [str(ROOT / "scripts/kernel"), "status"]:
                return SimpleNamespace(returncode=0, stdout="ready\n")
            if command == [str(ROOT / "scripts/doctor")]:
                return SimpleNamespace(returncode=0, stdout="[+] Host ready\n")
            if command == [str(ROOT / "scripts/kernel"), "ensure"]:
                return SimpleNamespace(returncode=0, stdout=None)
            if command == [
                str(ROOT / "vm/boot"),
                "--check",
                str(ROOT / "vm/checks/containment"),
            ]:
                return SimpleNamespace(returncode=0, stdout="[+] contained\n")
            self.fail(f"unexpected setup command: {command}")

        try:
            self.globals["run"] = fake_run
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                self.globals["cmd_setup"]([])
            setup_ready = self.globals["course_kernel_ready"]()
        finally:
            self.globals["run"] = original_run
        self.assertEqual(calls[0][0], [str(ROOT / "scripts/doctor")])
        self.assertEqual(calls[1][0], [str(ROOT / "scripts/kernel"), "ensure"])
        self.assertEqual(calls[2][0], [
            str(ROOT / "vm/boot"),
            "--check",
            str(ROOT / "vm/checks/containment"),
        ])
        self.assertTrue(calls[0][1]["capture"])
        self.assertFalse(calls[1][1].get("capture", False))
        self.assertTrue(calls[2][1]["capture"])
        self.assertTrue(setup_ready)

    def test_setup_stops_on_each_failed_check(self) -> None:
        original_run = self.globals["run"]
        cases = (
            [SimpleNamespace(returncode=1, stdout="doctor failed\n")],
            [
                SimpleNamespace(returncode=0, stdout="doctor ok\n"),
                SimpleNamespace(returncode=1, stdout=None),
            ],
            [
                SimpleNamespace(returncode=0, stdout="doctor ok\n"),
                SimpleNamespace(returncode=0, stdout=None),
                SimpleNamespace(returncode=1, stdout="guest failed\n"),
            ],
        )
        try:
            for results in cases:
                with self.subTest(results=len(results)):
                    queue = list(results)
                    self.globals["run"] = lambda *_args, **_kwargs: queue.pop(0)
                    with self.assertRaisesRegex(self.globals["CLIError"], "Setup failed"):
                        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                            self.globals["cmd_setup"]([])
        finally:
            self.globals["run"] = original_run

    def test_setup_rejects_invalid_argument_shapes(self) -> None:
        for arguments in (["--accept"], ["extra"]):
            with self.subTest(arguments=arguments):
                with self.assertRaisesRegex(self.globals["CLIError"], r"rootkitlings setup"):
                    self.globals["cmd_setup"](arguments)

    def test_missing_kernel_requires_setup(self) -> None:
        original_run = self.globals["run"]

        def fake_run(command, **_kwargs):
            if command == [str(ROOT / "scripts/kernel"), "status"]:
                return SimpleNamespace(returncode=1, stdout="missing\n")
            self.fail(f"unexpected setup status command: {command}")

        try:
            self.globals["run"] = fake_run
            self.assertFalse(self.globals["course_kernel_ready"]())
            with self.assertRaisesRegex(
                self.globals["CLIError"],
                r"Course kernel is not ready.*rootkitlings setup",
            ):
                self.globals["require_setup"]()
        finally:
            self.globals["run"] = original_run

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

    def test_status_reports_kernel_separately_from_course_count(self) -> None:
        output = io.StringIO()
        original_ready = self.globals["course_kernel_ready"]
        try:
            self.globals["course_kernel_ready"] = lambda: True
            with redirect_stdout(output):
                self.globals["cmd_status"]([])
        finally:
            self.globals["course_kernel_ready"] = original_ready
        lines = output.getvalue().splitlines()
        self.assertEqual(lines[0], "Rootkitlings")
        self.assertRegex(lines[1], r"^Course +0 of \d+ complete$")
        self.assertIn("Kernel     Ready", lines)
        self.assertIn("Workspace  Not started", lines)
        self.assertIn("  ./rootkitlings read L00", lines)

    def test_status_prioritizes_setup_when_kernel_is_missing(self) -> None:
        output = io.StringIO()
        original_ready = self.globals["course_kernel_ready"]
        try:
            self.globals["course_kernel_ready"] = lambda: False
            with redirect_stdout(output):
                self.globals["cmd_status"]([])
        finally:
            self.globals["course_kernel_ready"] = original_ready
        text = output.getvalue()
        self.assertIn("Kernel     Setup required", text)
        self.assertIn("  ./rootkitlings setup", text)
        self.assertNotIn("  ./rootkitlings read L00", text)

    def test_help_uses_short_reset_and_documents_current_default(self) -> None:
        output = io.StringIO()
        with redirect_stdout(output):
            self.globals["usage"]()
        help_text = output.getvalue()
        self.assertIn("reset --all --yes", help_text)
        self.assertNotIn("progress-reset", help_text)
        self.assertIn("current exercise is used", help_text)
        self.assertIn("Commands also accept unambiguous prefixes", help_text)

    def test_standard_help_flags_show_help(self) -> None:
        for flag in ("-h", "--help"):
            with self.subTest(flag=flag):
                output = io.StringIO()
                with redirect_stdout(output):
                    self.assertEqual(self.globals["main"]([flag]), 0)
                self.assertIn("./rootkitlings <COMMAND> [EXERCISE]", output.getvalue())

    def test_prereq_shows_only_assumptions_the_exercise_uses(self) -> None:
        output = io.StringIO()
        with redirect_stdout(output), redirect_stderr(io.StringIO()):
            self.globals["cmd_prereq"](["L00"])
        text = output.getvalue()
        self.assertIn("C functions and error handling:", text)
        self.assertIn("Shell commands and paths:", text)
        self.assertIn("Host, guest, and kernel boundary:", text)
        self.assertNotIn("IPv4 packet basics", text)
        self.assertNotIn("Before you begin", text)

    def test_prereq_shows_assumption_where_it_is_first_used(self) -> None:
        output = io.StringIO()
        with redirect_stdout(output), redirect_stderr(io.StringIO()):
            self.globals["cmd_prereq"](["L17"])
        text = output.getvalue()
        self.assertIn("IPv4 packet basics:", text)
        self.assertIn("Recognize an IPv4 header", text)

    def _build_exercise(self, stable_id="artifact-test", host_check=""):
        return SimpleNamespace(
            id=stable_id,
            serial="L99",
            workspace=SimpleNamespace(build=".", module_artifact="hello.ko"),
            resources=SimpleNamespace(host_check=host_check),
        )

    def test_build_pins_kdir_and_removes_exact_stale_artifact(self) -> None:
        exercise = self._build_exercise()
        workspace = self.globals["WORK"] / exercise.id
        workspace.mkdir()
        artifact = workspace / "rootkitlings.ko"
        artifact.write_bytes(b"stale")
        original_catalog = self.globals["CAT"]
        original_run = self.globals["run"]
        observed = []
        make_commands = []

        def fake_run(_command, **_kwargs):
            if _command == [str(ROOT / "scripts/kernel"), "build-dir"]:
                kernel = Path(self.temporary.name) / "kernel-build"
                kernel.mkdir(exist_ok=True)
                return SimpleNamespace(returncode=0, stdout=str(kernel) + "\n")
            observed.append(not artifact.exists())
            make_commands.append(_command)
            artifact.write_bytes(b"fresh")
            return SimpleNamespace(returncode=0)

        try:
            self.globals["CAT"] = SimpleNamespace(
                module_artifact=lambda _exercise: "rootkitlings.ko"
            )
            self.globals["run"] = fake_run
            with mock.patch.dict(os.environ, {"KDIR": "/tmp/untrusted-kdir"}):
                result = self.globals["build_workspace"](exercise)
        finally:
            self.globals["CAT"] = original_catalog
            self.globals["run"] = original_run
        self.assertEqual(result, artifact)
        self.assertEqual(artifact.read_bytes(), b"fresh")
        self.assertEqual(observed, [True])
        self.assertTrue(any(part.startswith("KDIR=") for part in make_commands[0]))
        self.assertNotIn("KDIR=/tmp/untrusted-kdir", make_commands[0])

    def test_failed_build_cannot_reuse_stale_artifact(self) -> None:
        exercise = self._build_exercise()
        workspace = self.globals["WORK"] / exercise.id
        workspace.mkdir()
        artifact = workspace / "hello.ko"
        artifact.write_bytes(b"stale")
        original_run = self.globals["run"]
        calls = 0

        def fail_build(command, **_kwargs):
            nonlocal calls
            calls += 1
            if calls == 1:
                kernel = Path(self.temporary.name) / "kernel-build"
                kernel.mkdir(exist_ok=True)
                return SimpleNamespace(returncode=0, stdout=str(kernel) + "\n")
            return SimpleNamespace(returncode=1)

        try:
            self.globals["run"] = fail_build
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
        repeated_success = SimpleNamespace(
            id="second",
            label="Second load",
            check=exercise.resources.check,
        )
        outcomes = (
            SimpleNamespace(
                case=success,
                build_root=build_root,
                artifact=artifact,
                output="",
            ),
            SimpleNamespace(
                case=repeated_success,
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
                "course_kernel_build_dir",
            )
        }
        attempts = []
        guest_runs = []
        host_runs = []
        try:
            self.globals["build_verification_cases"] = fake_builds
            self.globals["course_kernel_build_dir"] = lambda: build_root
            self.globals["run_host_check"] = (
                lambda *_args: host_runs.append(_args)
                or SimpleNamespace(returncode=0, stdout="")
            )
            self.globals["run"] = (
                lambda command, **_kwargs: guest_runs.append(command)
                or SimpleNamespace(returncode=0, stdout="[+] lifecycle\n")
            )
            self.globals["record_attempt"] = (
                lambda item, passed, *_args: attempts.append((item.id, passed)) or False
            )
            output = io.StringIO()
            with redirect_stdout(output), redirect_stderr(io.StringIO()):
                self.globals["cmd_check"]([exercise.serial])
        finally:
            self.globals.update(originals)
        self.assertIn("[*] Case 1 of 3  Normal load", output.getvalue())
        self.assertIn("[*] Case 2 of 3  Second load", output.getvalue())
        self.assertIn("[*] Case 3 of 3  Invalid build setting", output.getvalue())
        self.assertIn("Build rejected as required", output.getvalue())
        self.assertEqual(len(host_runs), 1)
        self.assertEqual(len(guest_runs), 2)
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

    def test_scaffold_materialization_uses_static_composed_starter(self) -> None:
        exercise = SimpleNamespace(
            id="consumer",
            workspace=SimpleNamespace(baseline="scaffold"),
        )
        calls = []
        original_catalog = self.globals["CAT"]
        try:
            self.globals["CAT"] = SimpleNamespace(
                materialize_starter=lambda *arguments: (
                    calls.append(arguments) or Path("/workspace")
                )
            )
            result = self.globals["materialize_workspace"](
                exercise,
                SimpleNamespace(),
            )
        finally:
            self.globals["CAT"] = original_catalog
        self.assertEqual(result, Path("/workspace"))
        self.assertEqual(calls, [(exercise, self.globals["WORK"], "consumer")])

    def test_edit_boundary_uses_the_recorded_start_not_reference_bytes(self) -> None:
        exercise = SimpleNamespace(
            workspace=SimpleNamespace(
                files=("editable.c", "inherited.c"),
                editable=("editable.c",),
            )
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            starting = root / "starting"
            frozen = root / "frozen"
            starting.mkdir()
            frozen.mkdir()
            (starting / "editable.c").write_text("starter\n")
            (starting / "inherited.c").write_text("valid alternative\n")
            (frozen / "editable.c").write_text("learner answer\n")
            (frozen / "inherited.c").write_text("valid alternative\n")

            self.globals["verify_edit_boundary"](exercise, frozen, starting)
            (frozen / "inherited.c").write_text("changed outside boundary\n")
            with self.assertRaisesRegex(
                self.globals["VerificationError"],
                "non-editable files changed: inherited.c",
            ):
                self.globals["verify_edit_boundary"](
                    exercise,
                    frozen,
                    starting,
                )

    def test_read_and_hint_use_the_current_exercise_without_changing_state(self) -> None:
        state_path = self.globals["STATE"]
        state_before = state_path.read_bytes() if state_path.exists() else None
        output = io.StringIO()
        with mock.patch.object(self.globals["shutil"], "which", return_value=None):
            with redirect_stdout(output):
                self.globals["cmd_read"]([])
        read_text = output.getvalue()
        self.assertIn("# Build, load, inspect, and unload a kernel module", read_text)
        self.assertIn("./rootkitlings start L00", read_text)

        output = io.StringIO()
        with redirect_stdout(output):
            self.globals["cmd_hint"]([])
        hint_text = output.getvalue()
        self.assertIn("Hint for L00", hint_text)
        self.assertIn("./rootkitlings check L00", hint_text)
        self.assertIn("./rootkitlings solution L00", hint_text)
        state_after = state_path.read_bytes() if state_path.exists() else None
        self.assertEqual(state_after, state_before)

        with redirect_stdout(io.StringIO()):
            self.globals["cmd_start"](["L00"])
        output = io.StringIO()
        with mock.patch.object(self.globals["shutil"], "which", return_value=None):
            with redirect_stdout(output):
                self.globals["cmd_read"](["L00"])
        self.assertIn("./rootkitlings check L00", output.getvalue())
        self.assertNotIn("./rootkitlings start L00", output.getvalue())

    def test_list_and_status_cover_started_advanced_and_complete_states(self) -> None:
        original_ready = self.globals["course_kernel_ready"]
        try:
            self.globals["course_kernel_ready"] = lambda: True
            with redirect_stdout(io.StringIO()):
                self.globals["cmd_start"](["L00"])

            output = io.StringIO()
            with redirect_stdout(output):
                self.globals["cmd_status"]([])
            status = output.getvalue()
            self.assertIn("Files to edit", status)
            self.assertIn("./rootkitlings check L00", status)

            with self.globals["STORE"].locked() as state:
                state.completed.add(self.globals["ROUTE"][0])
            output = io.StringIO()
            with redirect_stdout(output):
                self.globals["cmd_list"]([])
            listing = output.getvalue()
            self.assertRegex(listing, r"passed\s+L00")
            self.assertRegex(listing, r"current\s+L01")
            self.assertRegex(listing, r"locked\s+L02")
            self.assertIn("./rootkitlings read L01", listing)

            with self.globals["STORE"].locked() as state:
                state.completed.update(self.globals["ROUTE"])
            output = io.StringIO()
            with redirect_stdout(output):
                self.globals["cmd_status"]([])
            complete = output.getvalue()
            self.assertIn("Course complete", complete)
            self.assertNotIn("\nNext\n", complete)
        finally:
            self.globals["course_kernel_ready"] = original_ready

    def test_run_gdb_and_clean_forward_exact_workspace_arguments(self) -> None:
        exercise = self.globals["CAT"].by_id[self.globals["ROUTE"][0]]
        with redirect_stdout(io.StringIO()):
            self.globals["cmd_start"]([exercise.serial])
        artifact = self.globals["bd"](exercise) / self.globals["CAT"].module_artifact(exercise)
        artifact.write_bytes(b"module")
        kernel = Path(self.temporary.name) / "kernel"
        kernel.mkdir()
        original_build = self.globals["build_workspace"]
        original_kernel = self.globals["course_kernel_build_dir"]
        original_run = self.globals["run"]
        calls = []
        try:
            self.globals["build_workspace"] = lambda _exercise: artifact
            self.globals["course_kernel_build_dir"] = lambda: kernel
            self.globals["run"] = (
                lambda command, **kwargs: calls.append((command, kwargs))
                or SimpleNamespace(returncode=7, stdout="")
            )
            state_before = self.globals["STATE"].read_bytes()
            with self.assertRaises(SystemExit) as stopped:
                self.globals["cmd_run"]([exercise.serial, "--cap", "network"])
            self.assertEqual(stopped.exception.code, 7)
            run_command = calls[-1][0]
            self.assertEqual(run_command[0], str(ROOT / "vm/boot"))
            self.assertNotIn("--gdb", run_command)
            self.assertIn("--cap", run_command)
            self.assertIn("network", run_command)
            self.assertEqual(run_command[-1], str(artifact))

            with self.assertRaises(SystemExit):
                self.globals["cmd_run"]([exercise.serial], gdb=True)
            self.assertIn("--gdb", calls[-1][0])
            self.assertEqual(self.globals["STATE"].read_bytes(), state_before)

            self.globals["run"] = (
                lambda command, **kwargs: calls.append((command, kwargs))
                or SimpleNamespace(returncode=0, stdout="")
            )
            output = io.StringIO()
            with redirect_stdout(output):
                self.globals["cmd_clean"]([exercise.serial])
            clean_command = calls[-1][0]
            self.assertEqual(clean_command[:3], ["make", "-C", str(kernel)])
            self.assertIn(f"M={self.globals['bd'](exercise)}", clean_command)
            self.assertEqual(clean_command[-1], "clean")
            self.assertIn("Cleaned L00", output.getvalue())
        finally:
            self.globals["build_workspace"] = original_build
            self.globals["course_kernel_build_dir"] = original_kernel
            self.globals["run"] = original_run

    def test_run_argument_errors_and_command_prefixes_are_unambiguous(self) -> None:
        for arguments, message in (
            (["--cap"], "--cap requires"),
            (["--unknown"], "Unknown option"),
            (["L00", "L01"], "at most one exercise"),
        ):
            with self.subTest(arguments=arguments):
                with self.assertRaisesRegex(self.globals["CLIError"], message):
                    self.globals["cmd_run"](arguments)

        output = io.StringIO()
        with redirect_stdout(output):
            self.assertEqual(self.globals["main"](["prer", "L00"]), 0)
        self.assertIn("Prerequisites for L00", output.getvalue())
        with self.assertRaisesRegex(self.globals["CLIError"], "Ambiguous command"):
            self.globals["main"](["st"])
        with self.assertRaisesRegex(self.globals["CLIError"], "Unknown command"):
            self.globals["main"](["not-a-command"])
        with self.assertRaisesRegex(self.globals["CLIError"], "rootkitlings help"):
            self.globals["main"](["help", "extra"])

    def test_locked_required_lesson_opens_only_after_current_completion(self) -> None:
        first_id, second_id = self.globals["ROUTE"][:2]
        first = self.globals["CAT"].by_id[first_id]
        second = self.globals["CAT"].by_id[second_id]
        with self.globals["STORE"].read_locked() as state:
            with self.assertRaisesRegex(self.globals["CLIError"], "L01 is locked"):
                self.globals["available"](second, state)
        with self.globals["STORE"].locked() as state:
            state.completed.add(first.id)
        with self.globals["STORE"].read_locked() as state:
            self.globals["available"](second, state)
            self.globals["available"](first, state)

    def test_build_on_fresh_workspace_records_baseline_and_reopen_is_idempotent(self) -> None:
        exercise = self.globals["CAT"].by_id[self.globals["ROUTE"][0]]
        original_build = self.globals["build_workspace"]
        try:
            self.globals["build_workspace"] = lambda item: (
                self.globals["bd"](item) / self.globals["CAT"].module_artifact(item)
            )
            with redirect_stdout(io.StringIO()):
                self.globals["cmd_build"]([exercise.serial])
            with self.globals["STORE"].read_locked() as state:
                record = state.exercises[exercise.id]
                baseline = record.baseline_hash
                workspace_hash = record.workspace_hash
                self.assertTrue(baseline)
            output = io.StringIO()
            with redirect_stdout(output):
                self.globals["cmd_start"]([exercise.serial])
            self.assertIn("Workspace already exists", output.getvalue())
            with self.globals["STORE"].read_locked() as state:
                record = state.exercises[exercise.id]
                self.assertEqual(record.baseline_hash, baseline)
                self.assertEqual(record.workspace_hash, workspace_hash)
        finally:
            self.globals["build_workspace"] = original_build

    def test_successful_check_records_snapshot_and_advances_the_route(self) -> None:
        exercise = self.globals["CAT"].by_id[self.globals["ROUTE"][0]]
        with redirect_stdout(io.StringIO()):
            self.globals["cmd_start"]([exercise.serial])
        workspace = self.globals["WORK"] / exercise.id
        artifact = self.globals["bd"](exercise) / self.globals["CAT"].module_artifact(exercise)
        artifact.write_bytes(b"module")
        case = SimpleNamespace(
            id="success",
            label="Complete behavior",
            check=exercise.resources.check,
            check_arguments=(),
        )
        outcome = SimpleNamespace(case=case, build_root=workspace, artifact=artifact)

        @contextmanager
        def successful_build(*_args, **_kwargs):
            source_hash = self.globals["canonical_tree_hash"](
                workspace,
                exercise.workspace.files,
            )
            yield SimpleNamespace(
                outcomes=(outcome,),
                frozen_source=workspace,
                source_hash=source_hash,
            )

        originals = {
            name: self.globals[name]
            for name in (
                "build_verification_cases", "course_kernel_build_dir",
                "run_host_check", "run",
            )
        }
        try:
            self.globals["build_verification_cases"] = successful_build
            self.globals["course_kernel_build_dir"] = lambda: workspace
            self.globals["run_host_check"] = (
                lambda *_args: SimpleNamespace(returncode=0, stdout="")
            )
            self.globals["run"] = (
                lambda *_args, **_kwargs: SimpleNamespace(returncode=0, stdout="")
            )
            with redirect_stdout(io.StringIO()):
                self.globals["cmd_check"]([exercise.serial])
        finally:
            self.globals.update(originals)
        with self.globals["STORE"].read_locked() as state:
            record = state.exercises[exercise.id]
            self.assertEqual(record.attempts, 1)
            self.assertTrue(record.passed_snapshot)
            self.assertIn(exercise.id, state.completed)
            self.assertEqual(state.current, self.globals["ROUTE"][1])

    def test_restore_archives_edits_without_changing_progress_fields(self) -> None:
        exercise = self.globals["CAT"].by_id[self.globals["ROUTE"][0]]
        with redirect_stdout(io.StringIO()):
            self.globals["cmd_start"]([exercise.serial])
        editable = self.globals["WORK"] / exercise.id / exercise.workspace.editable[0]
        original = editable.read_bytes()
        editable.write_bytes(original + b"\nchanged\n")
        with self.globals["STORE"].locked() as state:
            record = state.exercise(exercise.id)
            record.attempts = 4
            record.solution_revealed = True
            state.completed.add(exercise.id)
        with redirect_stdout(io.StringIO()):
            self.globals["cmd_restore"]([exercise.serial])
        self.assertEqual(editable.read_bytes(), original)
        self.assertEqual(len(tuple(self.globals["ARCHIVE"].iterdir())), 1)
        with self.globals["STORE"].read_locked() as state:
            record = state.exercises[exercise.id]
            self.assertEqual(record.attempts, 4)
            self.assertTrue(record.solution_revealed)
            self.assertIn(exercise.id, state.completed)


if __name__ == "__main__":
    unittest.main()
