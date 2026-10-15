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
            run_name="rklings_test",
        )
        self.globals = namespace["cmd_reset"].__globals__
        self.temporary = tempfile.TemporaryDirectory()
        runtime = Path(self.temporary.name) / ".rklings"
        values = {
            "RT": runtime,
            "STATE": runtime / "state.toml",
            "WORK": Path(self.temporary.name) / "work",
            "BUILD": runtime / "build",
            "SNAPSHOTS": runtime / "snapshots",
            "BACKUPS": runtime / "backups",
        }
        self.globals.update(values)
        self.globals["STORE"] = self.globals["StateStore"](
            values["STATE"],
            self.globals["ROUTE"],
            self.globals["CAT"].revision,
            self.globals["COMPLETION_DEPENDENCIES"],
        )
        self.globals["initialize_runtime"]()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _workspace(self, stable_id: str, content: str) -> Path:
        exercise = self.globals["CAT"].by_id.get(stable_id)
        name = (
            self.globals["workspace_name"](exercise)
            if exercise is not None
            else stable_id
        )
        workspace = self.globals["WORK"] / name
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
                with self.assertRaisesRegex(self.globals["CLIError"], r"rklings setup"):
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
                r"Course kernel is not ready.*rklings setup",
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
                "solution", "run", "connect", "gdb", "status", "list",
                "prereq",
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
                    command in {"build", "check", "run", "connect", "gdb"},
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
        self.assertEqual(lines[0], "Rklings")
        self.assertRegex(lines[1], r"^Course +0 of \d+ complete$")
        self.assertIn("Kernel     Ready", lines)
        self.assertIn("Workspace  Not started", lines)
        self.assertIn("  ./rklings read L00", lines)

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
        self.assertIn("  ./rklings setup", text)
        self.assertNotIn("  ./rklings read L00", text)

    def test_help_uses_short_reset_and_documents_current_default(self) -> None:
        output = io.StringIO()
        with redirect_stdout(output):
            self.globals["usage"]()
        help_text = output.getvalue()
        self.assertIn("reset --all --yes", help_text)
        self.assertNotIn("progress-reset", help_text)
        self.assertIn("current exercise is used", help_text)
        self.assertIn("or a unique prefix", help_text)
        self.assertIn("run ID --reference", help_text)
        self.assertIn("connect [EXERCISE]", help_text)

    def test_standard_help_flags_show_help(self) -> None:
        for flag in ("-h", "--help"):
            with self.subTest(flag=flag):
                output = io.StringIO()
                with redirect_stdout(output):
                    self.assertEqual(self.globals["main"]([flag]), 0)
                self.assertIn("./rklings <COMMAND> [EXERCISE]", output.getvalue())

    def test_standard_version_flags_show_the_catalog_revision(self) -> None:
        for flag in ("-V", "--version"):
            with self.subTest(flag=flag):
                output = io.StringIO()
                with redirect_stdout(output):
                    self.assertEqual(self.globals["main"]([flag]), 0)
                self.assertEqual(
                    output.getvalue(),
                    f"rklings curriculum {self.globals['CAT'].revision}\n",
                )

    def test_prereq_shows_only_assumptions_the_exercise_uses(self) -> None:
        output = io.StringIO()
        with redirect_stdout(output), redirect_stderr(io.StringIO()):
            self.globals["cmd_prereq"](["L00"])
        text = output.getvalue()
        self.assertIn("C Functions And Errors", text)
        self.assertIn("Shell And Paths", text)
        self.assertIn("Host Guest Kernel Boundary", text)
        self.assertNotIn("IPv4", text)
        self.assertNotIn("Before you begin", text)

    def test_prereq_shows_assumption_where_it_is_first_used(self) -> None:
        output = io.StringIO()
        with redirect_stdout(output), redirect_stderr(io.StringIO()):
            self.globals["cmd_prereq"](["L01"])
        text = output.getvalue()
        self.assertIn("C Preprocessor", text)
        self.assertIn("Read macro calls", text)

    def _build_exercise(self, stable_id="artifact-test", host_check=""):
        return SimpleNamespace(
            id=stable_id,
            serial="L99",
            workspace=SimpleNamespace(build=".", module_artifact="hello.ko"),
            resources=SimpleNamespace(host_check=host_check),
        )

    def test_build_pins_kdir_and_removes_exact_stale_artifact(self) -> None:
        exercise = self._build_exercise()
        workspace = self.globals["wp"](exercise)
        workspace.mkdir()
        artifact = self.globals["build_output"](exercise) / "rklings.ko"
        artifact.parent.mkdir(parents=True)
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
                module_artifact=lambda _exercise: "rklings.ko"
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
        workspace = self.globals["wp"](exercise)
        workspace.mkdir()
        artifact = self.globals["build_output"](exercise) / "hello.ko"
        artifact.parent.mkdir(parents=True)
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
        workspace = self.globals["wp"](exercise)
        workspace.mkdir()
        artifact = self.globals["build_output"](exercise) / "hello.ko"
        artifact.parent.mkdir(parents=True)
        target = artifact.parent / "outside"
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
        workspace = self.globals["wp"](exercise)
        workspace.mkdir()
        artifact = workspace / "rklings.ko"
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
        self.assertEqual(
            kwargs,
            {"timeout": 17, "capture": True, "visible": True},
        )

    def test_check_runs_each_declarative_case_and_records_one_attempt(self) -> None:
        exercise = self.globals["CAT"].required()[0]
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            self.globals["cmd_start"]([exercise.serial])
        build_root = self.globals["wp"](exercise)
        artifact = build_root / self.globals["CAT"].module_artifact(exercise)
        artifact.write_bytes(b"module")
        success = SimpleNamespace(
            id="normal",
            label="Normal load",
            check=exercise.verification.cases[0].check,
            check_arguments=(),
        )
        rejection = SimpleNamespace(
            id="invalid",
            label="Invalid build setting",
            check="",
        )
        repeated_success = SimpleNamespace(
            id="second",
            label="Second load",
            check=exercise.verification.cases[0].check,
            check_arguments=(),
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
        self.assertIn("1/3 Normal load", output.getvalue())
        self.assertIn("2/3 Second load", output.getvalue())
        self.assertIn("3/3 Invalid build setting", output.getvalue())
        self.assertIn("Build rejected as required", output.getvalue())
        self.assertEqual(len(host_runs), 1)
        self.assertEqual(len(guest_runs), 2)
        self.assertEqual(attempts, [(exercise.id, True)])

    def test_missing_workspace_is_not_an_attempt(self) -> None:
        exercise = self.globals["CAT"].required()[0]
        with self.assertRaisesRegex(self.globals["CLIError"], "has no workspace"):
            self.globals["cmd_check"]([exercise.serial])
        with self.globals["STORE"].read_locked() as state:
            self.assertNotIn(exercise.id, state.exercises)

    def test_changed_workspace_records_one_failed_attempt_not_pass(self) -> None:
        exercise = self.globals["CAT"].required()[0]
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            self.globals["cmd_start"]([exercise.serial])
        workspace = self.globals["wp"](exercise)
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
        exercise = self.globals["CAT"].required()[0]
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            self.globals["cmd_start"]([exercise.serial])
        workspace = self.globals["wp"](exercise)
        artifact = workspace / self.globals["CAT"].module_artifact(exercise)
        artifact.write_bytes(b"module")
        outcome = SimpleNamespace(
            case=SimpleNamespace(
                label="Control",
                check=exercise.verification.cases[0].check,
                check_arguments=(),
            ),
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

    def test_starter_solution_renders_recursive_paths_in_order(self) -> None:
        exercise = SimpleNamespace(
            id="starter",
            serial="L00",
            title="Starter lesson",
            workspace=SimpleNamespace(baseline="starter"),
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
            )
            output = self.globals["solution_output"](exercise)
        finally:
            self.globals["CAT"] = original_catalog
        self.assertIn("Complete reference files", output)
        self.assertNotIn("recorded", output)
        headings = (
            "--- Kbuild ---",
            "--- core/main.c ---",
            "--- status/main.c ---",
        )
        positions = [output.index(value) for value in headings]
        self.assertEqual(positions, sorted(positions))
        self.assertNotIn("--- main.c ---", output)

    def test_solution_view_does_not_mutate_learner_state(self) -> None:
        exercise = self.globals["CAT"].required()[0]
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

            state_path = self.globals["STATE"]
            before = state_path.read_bytes() if state_path.exists() else None
            future = self.globals["CAT"].required()[-1]
            self.globals["solution_output"] = lambda *_args: "rendered\n"
            with redirect_stdout(io.StringIO()):
                self.globals["cmd_solution"]([future.serial])
            after = state_path.read_bytes() if state_path.exists() else None
            self.assertEqual(after, before)
        finally:
            self.globals["solution_output"] = original_output

    def test_reference_run_does_not_mutate_progress(self) -> None:
        item = self.globals["CAT"].by_id[self.globals["ROUTE"][-1]]
        if item.runtime is None:
            self.skipTest("final exercise has no runtime metadata")
        first = self.globals["CAT"].by_id[self.globals["ROUTE"][0]]
        with self.globals["STORE"].locked() as state:
            state.exercise(first.id).attempts = 2
        workspace = self._workspace(first.id, "learner work\n")
        marker = workspace / "learner.c"
        before_state = self.globals["STATE"].read_bytes()
        before_workspace = marker.read_bytes()
        calls: dict[str, object] = {}
        catalog = self.globals["CAT"]
        original_materialize = catalog.materialize_solution
        original_build = self.globals["build_source"]
        original_session = self.globals["create_target_session"]
        original_wait = self.globals["wait_for_operator"]
        original_run = self.globals["run"]

        def fake_materialize(selected, destination, stable_id):
            calls["materialize"] = (selected, destination, stable_id)
            source = destination / stable_id
            source.mkdir()
            return source

        def fake_build(selected, source, destination):
            calls["build"] = (selected, source, destination)
            module = destination / "rklings.ko"
            module.parent.mkdir()
            module.write_bytes(b"reference module")
            return module

        class FakeTarget:
            key_file = ROOT / ".rklings/run/P00/control.key"
            control_port = 40001
            shell_port = 40002

            @staticmethod
            def close():
                calls["closed"] = True

        def fake_session(*args):
            calls["session"] = args
            target = FakeTarget()
            calls["target"] = target
            return target

        def fake_run(command, **kwargs):
            calls["run"] = (command, kwargs)
            return SimpleNamespace(returncode=0)

        try:
            catalog.materialize_solution = fake_materialize
            self.globals["build_source"] = fake_build
            self.globals["create_target_session"] = fake_session
            self.globals["wait_for_operator"] = lambda target: calls.update(
                waited=target
            )
            self.globals["run"] = fake_run
            with redirect_stdout(io.StringIO()):
                self.globals["cmd_run"]([item.serial, "--reference"])
        finally:
            catalog.materialize_solution = original_materialize
            self.globals["build_source"] = original_build
            self.globals["create_target_session"] = original_session
            self.globals["wait_for_operator"] = original_wait
            self.globals["run"] = original_run

        self.assertEqual(self.globals["STATE"].read_bytes(), before_state)
        self.assertEqual(marker.read_bytes(), before_workspace)
        self.assertEqual(calls["materialize"][0], item)
        self.assertEqual(calls["materialize"][2], "source")
        self.assertEqual(calls["session"], (ROOT, item.serial))
        self.assertIs(calls["waited"], calls["target"])
        command, run_kwargs = calls["run"]
        self.assertIn("--runtime-setup", command)
        self.assertIn(str(ROOT / item.runtime.setup), command)
        self.assertTrue(Path(command[-1]).name.endswith(".ko"))
        self.assertEqual(run_kwargs, {"visible": True})
        self.assertTrue(calls["closed"])

    def test_required_reset_archives_and_clears_suffix_only(self) -> None:
        route = self.globals["ROUTE"]
        earlier, selected, later = route[:3]
        with self.globals["STORE"].locked() as state:
            state.completed.update((earlier, selected, later))
            for stable_id in (earlier, selected, later):
                record = state.exercise(stable_id)
                record.attempts = 2
                record.baseline_hash = "a" * 64

        earlier_workspace = self._workspace(earlier, "keep\n")
        selected_workspace = self._workspace(selected, "archive selected\n")
        later_workspace = self._workspace(later, "archive later\n")
        marker = self.globals["SNAPSHOTS"] / "untouched"
        marker.write_text("snapshot", encoding="utf-8")

        output = self._reset([selected])

        with self.globals["STORE"].read_locked() as state:
            self.assertEqual(state.completed, {earlier})
            self.assertEqual(set(state.exercises), {earlier})
        self.assertTrue(earlier_workspace.exists())
        self.assertFalse(selected_workspace.exists())
        self.assertFalse(later_workspace.exists())
        self.assertTrue(marker.exists())
        archives = list(self.globals["BACKUPS"].iterdir())
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

    def test_reset_never_removes_an_unrecorded_visible_directory(self) -> None:
        exercise = self.globals["CAT"].by_id[self.globals["ROUTE"][0]]
        workspace = self.globals["wp"](exercise)
        workspace.mkdir()
        marker = workspace / "personal.txt"
        marker.write_text("not owned by course state\n", encoding="utf-8")

        self._reset([exercise.serial])

        self.assertEqual(
            marker.read_text(encoding="utf-8"),
            "not owned by course state\n",
        )

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
        exercise = self.globals["CAT"].by_id[self.globals["ROUTE"][0]]
        serial = exercise.serial
        state_path = self.globals["STATE"]
        state_before = state_path.read_bytes() if state_path.exists() else None
        output = io.StringIO()
        with redirect_stdout(output):
            self.globals["cmd_read"]([])
        read_text = output.getvalue()
        self.assertIn(f"# {exercise.title}", read_text)
        self.assertIn(f"./rklings start {serial}", read_text)

        output = io.StringIO()
        with redirect_stdout(output):
            self.globals["cmd_hint"]([])
        hint_text = output.getvalue()
        self.assertIn(f"Hint for {serial}", hint_text)
        self.assertIn(f"./rklings start {serial}", hint_text)
        self.assertIn(f"./rklings solution {serial}", hint_text)
        state_after = state_path.read_bytes() if state_path.exists() else None
        self.assertEqual(state_after, state_before)

        with redirect_stdout(io.StringIO()):
            self.globals["cmd_start"]([serial])
        output = io.StringIO()
        with redirect_stdout(output):
            self.globals["cmd_read"]([serial])
        self.assertIn(f"./rklings check {serial}", output.getvalue())
        self.assertNotIn(f"./rklings start {serial}", output.getvalue())

    def test_start_uses_a_short_visible_workspace_and_complete_edit_paths(self) -> None:
        exercise = self.globals["CAT"].by_id[self.globals["ROUTE"][0]]
        output = io.StringIO()
        with redirect_stdout(output):
            self.globals["cmd_start"]([exercise.serial])

        text = output.getvalue()
        workspace = self.globals["wp"](exercise)
        self.assertIn(f"Workspace created: {workspace}", text)
        for relative in exercise.workspace.editable:
            self.assertIn(f"- {workspace}/{relative}", text)
        self.assertNotIn(".rklings/work", text)
        self.assertEqual(
            workspace,
            self.globals["WORK"] / exercise.serial,
        )

    def test_read_uses_bat_without_paging_and_respects_redirected_output(self) -> None:
        calls = []
        original_which = self.globals["shutil"].which
        original_run = self.globals["run"]
        try:
            self.globals["shutil"].which = (
                lambda name: "/usr/bin/batcat" if name == "batcat" else None
            )
            self.globals["run"] = (
                lambda command, **kwargs: calls.append((command, kwargs))
                or SimpleNamespace(returncode=0, stdout="# highlighted\n")
            )
            output = io.StringIO()
            with redirect_stdout(output):
                self.globals["cmd_read"](["L00"])
        finally:
            self.globals["shutil"].which = original_which
            self.globals["run"] = original_run
        command, kwargs = calls[0]
        self.assertEqual(command[0], "/usr/bin/batcat")
        self.assertIn("--style=plain", command)
        self.assertIn("--paging=never", command)
        self.assertIn("--color=never", command)
        self.assertIn("--terminal-width=80", command)
        self.assertIn("--language=markdown", command)
        self.assertEqual(kwargs, {"capture": True})
        self.assertIn("# highlighted", output.getvalue())

    def test_read_shows_renderer_failure_then_plain_markdown(self) -> None:
        exercise = self.globals["CAT"].by_id[self.globals["ROUTE"][0]]
        original_which = self.globals["shutil"].which
        original_run = self.globals["run"]
        try:
            self.globals["shutil"].which = lambda _name: "/usr/bin/bat"
            self.globals["run"] = lambda *_args, **_kwargs: SimpleNamespace(
                returncode=2,
                stdout="renderer error\n",
            )
            output = io.StringIO()
            error = io.StringIO()
            with redirect_stdout(output), redirect_stderr(error):
                self.globals["cmd_read"]([exercise.serial])
        finally:
            self.globals["shutil"].which = original_which
            self.globals["run"] = original_run

        rendered = output.getvalue()
        failure = error.getvalue()
        self.assertIn("Syntax highlighting failed", failure)
        self.assertIn("renderer error", failure)
        self.assertIn("$ /usr/bin/bat", rendered)
        self.assertRegex(rendered, r"Exit\s+2")
        self.assertIn(
            f"# {exercise.title}",
            rendered,
        )

    def test_list_and_status_cover_started_advanced_and_complete_states(self) -> None:
        first = self.globals["CAT"].by_id[self.globals["ROUTE"][0]]
        second = self.globals["CAT"].by_id[self.globals["ROUTE"][1]]
        third = self.globals["CAT"].by_id[self.globals["ROUTE"][2]]
        original_ready = self.globals["course_kernel_ready"]
        try:
            self.globals["course_kernel_ready"] = lambda: True
            with redirect_stdout(io.StringIO()):
                self.globals["cmd_start"]([first.serial])

            output = io.StringIO()
            with redirect_stdout(output):
                self.globals["cmd_status"]([])
            status = output.getvalue()
            self.assertIn("Files to edit", status)
            for relative in first.workspace.editable:
                self.assertIn(f"work/{first.serial}/{relative}", status)
            self.assertRegex(status, r"State +Unchanged")
            self.assertIn(f"./rklings check {first.serial}", status)

            editable = self.globals["wp"](
                self.globals["CAT"].by_id[self.globals["ROUTE"][0]]
            ) / "core/main.c"
            editable.write_text("modified\n", encoding="utf-8")
            output = io.StringIO()
            with redirect_stdout(output):
                self.globals["cmd_status"]([])
            self.assertRegex(output.getvalue(), r"State +Modified")

            with self.globals["STORE"].locked() as state:
                state.completed.add(self.globals["ROUTE"][0])
            output = io.StringIO()
            with redirect_stdout(output):
                self.globals["cmd_list"]([])
            listing = output.getvalue()
            self.assertRegex(listing, rf"passed\s+{first.serial}")
            self.assertRegex(listing, rf"current\s+{second.serial}")
            self.assertRegex(listing, rf"locked\s+{third.serial}")
            self.assertIn(
                f"./rklings read {second.serial}",
                listing,
            )

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
        artifact = self.globals["build_output"](exercise) / self.globals["CAT"].module_artifact(exercise)
        artifact.parent.mkdir(parents=True)
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
            self.assertFalse(self.globals["build_output"](exercise).exists())
            self.assertTrue(self.globals["wp"](exercise).is_dir())
            self.assertIn("Cleaned build outputs for L00", output.getvalue())
            self.assertIn("work/L00", output.getvalue())
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
        with self.assertRaisesRegex(self.globals["CLIError"], "rklings help"):
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

    def test_build_requires_an_explicitly_started_workspace(self) -> None:
        exercise = self.globals["CAT"].by_id[self.globals["ROUTE"][0]]
        with self.assertRaisesRegex(
            self.globals["CLIError"],
            "Run: ./rklings start L00",
        ):
            with redirect_stdout(io.StringIO()):
                self.globals["cmd_build"]([exercise.serial])

    def test_successful_check_records_snapshot_and_advances_the_route(self) -> None:
        exercise = self.globals["CAT"].by_id[self.globals["ROUTE"][0]]
        with redirect_stdout(io.StringIO()):
            self.globals["cmd_start"]([exercise.serial])
        workspace = self.globals["wp"](exercise)
        artifact = workspace / self.globals["CAT"].module_artifact(exercise)
        artifact.write_bytes(b"module")
        case = SimpleNamespace(
            id="success",
            label="Complete behavior",
            check=exercise.verification.cases[0].check,
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
        editable = self.globals["wp"](exercise) / exercise.workspace.editable[0]
        original = editable.read_bytes()
        editable.write_bytes(original + b"\nchanged\n")
        with self.globals["STORE"].locked() as state:
            record = state.exercise(exercise.id)
            record.attempts = 4
            state.completed.add(exercise.id)
        with redirect_stdout(io.StringIO()):
            self.globals["cmd_restore"]([exercise.serial])
        self.assertEqual(editable.read_bytes(), original)
        self.assertEqual(len(tuple(self.globals["BACKUPS"].iterdir())), 1)
        with self.globals["STORE"].read_locked() as state:
            record = state.exercises[exercise.id]
            self.assertEqual(record.attempts, 4)
            self.assertIn(exercise.id, state.completed)


if __name__ == "__main__":
    unittest.main()
