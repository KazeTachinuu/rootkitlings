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
            self.globals["PREREQUISITES"],
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
            )
            self.globals["ROUTE"] = ("required",)
            affected = self.globals["progress_reset_ids"](elective)
        finally:
            self.globals["CAT"] = original_catalog
            self.globals["ROUTE"] = original_route
        self.assertEqual(affected, {"elective", "descendant"})


if __name__ == "__main__":
    unittest.main()
