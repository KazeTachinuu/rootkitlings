from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from lib.catalog import VerificationCase
from lib.verification import VerificationError, build_verification_cases
from lib.workspace import materialize as real_materialize


class VerificationBuildTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.repository = self.root / "repository"
        self.source = self.root / "source"
        self.repository.mkdir()
        self.source.mkdir()
        self.kernel_build = self.root / "kernel-build"
        self.kernel_build.mkdir()
        (self.source / "Kbuild").write_text("build\n", encoding="utf-8")
        (self.source / "main.c").write_text("original\n", encoding="utf-8")
        (self.source / "ignored.c").write_text("ignored\n", encoding="utf-8")
        (self.repository / "Makefile").write_text(
            """\
.PHONY: module
module:
\t@case \"$(MODE)\" in \\
\t  pass) printf '%s\\n' \"$(VALUE)\" >> \"$(MODULE_DIR)/main.c\"; printf '%s' \"$(KDIR)\" > \"$(MODULE_DIR)/kdir\"; printf '%s|%s|%s|%s' \"$(ARCH)\" \"$(CC)\" \"$(LC_ALL)\" \"$(KCFLAGS)\" > \"$(MODULE_DIR)/environment\"; printf module > \"$(MODULE_DIR)/test.ko\" ;; \\
\t  reject) printf '%s\\n' \"expected marker\"; exit 2 ;; \\
\t  reject-other) printf '%s\\n' \"different output\"; exit 2 ;; \\
\t  warning) printf '%s\\n' \"warning: test warning\"; printf module > \"$(MODULE_DIR)/test.ko\" ;; \\
\t  missing) : ;; \\
\t  symlink) ln -s main.c \"$(MODULE_DIR)/test.ko\" ;; \\
\t  special) mkfifo \"$(MODULE_DIR)/test.ko\" ;; \\
\t  timeout) sleep 2 ;; \\
\t  fail) exit 2 ;; \\
\t  fail-artifact) printf module > "$(MODULE_DIR)/test.ko"; exit 2 ;; \\
\t  *) exit 3 ;; \\
\tesac
""",
            encoding="utf-8",
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def case(
        self,
        case_id: str,
        mode: str,
        *,
        expect: str = "pass",
        marker: str = "",
        value: str = "",
    ) -> VerificationCase:
        variables = (("MODE", mode), ("VALUE", value))
        check = "guest-check" if expect == "pass" else ""
        return VerificationCase(case_id, case_id, variables, expect, check, marker)

    def builds(self, cases, timeout=5):
        return build_verification_cases(
            self.repository,
            self.source,
            ("Kbuild", "main.c"),
            ".",
            "test.ko",
            cases,
            timeout,
            kernel_build_dir=self.kernel_build,
        )

    def test_cases_are_isolated_and_variables_are_argv_values(self) -> None:
        first = self.case("first", "pass", value="one value")
        second = self.case("second", "pass", value="two")
        with self.builds((first, second)) as verification:
            outcomes = verification.outcomes
            self.assertEqual(len(outcomes), 2)
            self.assertEqual(
                (outcomes[0].build_root / "main.c").read_text(),
                "original\none value\n",
            )
            self.assertEqual(
                (outcomes[1].build_root / "main.c").read_text(),
                "original\ntwo\n",
            )
            self.assertFalse((outcomes[0].build_root / "ignored.c").exists())
            self.assertEqual(
                (verification.frozen_source / "main.c").read_text(),
                "original\n",
            )
            self.assertTrue(verification.source_hash)
            retained = outcomes[0].build_root
            self.assertTrue(outcomes[0].artifact.is_file())
            self.assertEqual(
                (outcomes[0].build_root / "kdir").read_text(),
                str(self.kernel_build.resolve()),
            )
            self.assertEqual(
                (outcomes[0].build_root / "environment").read_text(),
                "x86_64|gcc|C|",
            )
        self.assertFalse(retained.exists())

    def test_expected_rejection_and_optional_marker(self) -> None:
        cases = (
            self.case("marked", "reject", expect="fail", marker="expected marker"),
            self.case("unmarked", "reject-other", expect="fail"),
        )
        with self.builds(cases) as verification:
            outcomes = verification.outcomes
            self.assertEqual([item.artifact for item in outcomes], [None, None])
            self.assertIn("expected marker", outcomes[0].output)

    def test_required_marker_must_appear(self) -> None:
        with self.assertRaisesRegex(VerificationError, "lacks required marker"):
            with self.builds((self.case("bad", "reject-other", expect="fail", marker="wanted"),)):
                pass

    def test_unexpected_pass_and_failure_are_rejected(self) -> None:
        for case, message in (
            (self.case("pass", "pass", expect="fail"), "unexpectedly built"),
            (self.case("fail", "fail"), "unexpectedly failed"),
        ):
            with self.subTest(case=case.id):
                with self.assertRaisesRegex(VerificationError, message):
                    with self.builds((case,)):
                        pass

    def test_successful_build_with_warning_is_rejected(self) -> None:
        with self.assertRaisesRegex(VerificationError, "built with warnings"):
            with self.builds((self.case("warning", "warning"),)):
                pass

    def test_missing_symlink_and_special_artifacts_are_rejected(self) -> None:
        for mode in ("missing", "symlink", "special"):
            with self.subTest(mode=mode):
                with self.assertRaisesRegex(VerificationError, "artifact"):
                    with self.builds((self.case(mode, mode),)):
                        pass

    def test_timeout_is_rejected_and_tree_is_cleaned(self) -> None:
        before = set(Path(tempfile.gettempdir()).glob("rootkitlings-cases-*"))
        with self.assertRaisesRegex(VerificationError, "timed out"):
            with self.builds((self.case("slow", "timeout"),), timeout=1):
                pass
        after = set(Path(tempfile.gettempdir()).glob("rootkitlings-cases-*"))
        self.assertEqual(before, after)

    def test_preexisting_declared_artifact_is_not_fresh(self) -> None:
        (self.source / "test.ko").write_text("old", encoding="utf-8")
        with self.assertRaisesRegex(VerificationError, "starts with an artifact"):
            with build_verification_cases(
                self.repository,
                self.source,
                ("Kbuild", "main.c", "test.ko"),
                ".",
                "test.ko",
                (self.case("old", "pass"),),
                5,
                kernel_build_dir=self.kernel_build,
            ):
                pass

    def test_failed_build_must_not_leave_artifact(self) -> None:
        case = self.case("failed", "fail-artifact", expect="fail")
        with self.assertRaisesRegex(VerificationError, "left an artifact"):
            with self.builds((case,)):
                pass

    def test_source_change_while_freezing_is_rejected(self) -> None:
        def changing_materialize(source, parent, stable_id, files):
            result = real_materialize(source, parent, stable_id, files)
            if stable_id == "frozen":
                (self.source / "main.c").write_text("changed\n", encoding="utf-8")
            return result

        with patch("lib.verification.materialize", changing_materialize):
            with self.assertRaisesRegex(VerificationError, "changed while"):
                with self.builds((self.case("pass", "pass"),)):
                    pass

    def test_all_cases_use_frozen_source_after_live_change(self) -> None:
        original_materialize = real_materialize

        def changing_live_source(source, parent, stable_id, files):
            if stable_id == "first":
                (self.source / "main.c").write_text("live changed\n", encoding="utf-8")
            return original_materialize(source, parent, stable_id, files)

        cases = (
            self.case("first", "pass", value="one"),
            self.case("second", "pass", value="two"),
        )
        with patch("lib.verification.materialize", changing_live_source):
            with self.builds(cases) as verification:
                first, second = verification.outcomes
                self.assertEqual(
                    (first.build_root / "main.c").read_text(),
                    "original\none\n",
                )
                self.assertEqual(
                    (second.build_root / "main.c").read_text(),
                    "original\ntwo\n",
                )


if __name__ == "__main__":
    unittest.main()
