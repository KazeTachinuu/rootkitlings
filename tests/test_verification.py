from pathlib import Path
import tempfile
import unittest

from lib.catalog import VerificationCase
from lib.verification import VerificationError, build_verification_cases


class VerificationBuildTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.repository = self.root / "repository"
        self.source = self.root / "source"
        self.repository.mkdir()
        self.source.mkdir()
        (self.source / "Kbuild").write_text("build\n", encoding="utf-8")
        (self.source / "main.c").write_text("original\n", encoding="utf-8")
        (self.source / "ignored.c").write_text("ignored\n", encoding="utf-8")
        (self.repository / "Makefile").write_text(
            """\
.PHONY: module
module:
\t@case \"$(MODE)\" in \\
\t  pass) printf '%s\\n' \"$(VALUE)\" >> \"$(MODULE_DIR)/main.c\"; printf module > \"$(MODULE_DIR)/test.ko\" ;; \\
\t  reject) printf '%s\\n' \"expected marker\"; exit 2 ;; \\
\t  reject-other) printf '%s\\n' \"different output\"; exit 2 ;; \\
\t  warning) printf '%s\\n' \"warning: test warning\"; printf module > \"$(MODULE_DIR)/test.ko\" ;; \\
\t  missing) : ;; \\
\t  symlink) ln -s main.c \"$(MODULE_DIR)/test.ko\" ;; \\
\t  special) mkfifo \"$(MODULE_DIR)/test.ko\" ;; \\
\t  timeout) sleep 2 ;; \\
\t  fail) exit 2 ;; \\
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
        )

    def test_cases_are_isolated_and_variables_are_argv_values(self) -> None:
        first = self.case("first", "pass", value="one value")
        second = self.case("second", "pass", value="two")
        with self.builds((first, second)) as outcomes:
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
            retained = outcomes[0].build_root
            self.assertTrue(outcomes[0].artifact.is_file())
        self.assertFalse(retained.exists())

    def test_expected_rejection_and_optional_marker(self) -> None:
        cases = (
            self.case("marked", "reject", expect="fail", marker="expected marker"),
            self.case("unmarked", "reject-other", expect="fail"),
        )
        with self.builds(cases) as outcomes:
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
            ):
                pass


if __name__ == "__main__":
    unittest.main()
