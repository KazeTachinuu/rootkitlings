from __future__ import annotations

from pathlib import Path
import runpy
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
RUNNER = runpy.run_path(str(ROOT / "scripts/test-solutions"))
SolutionCheckError = RUNNER["SolutionCheckError"]
require_compiled_sources = RUNNER["require_compiled_sources"]


class CompiledSourceTests(unittest.TestCase):
    def test_every_declared_c_source_must_appear_in_kbuild(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            (source / "core").mkdir()
            (source / "core/main.c").write_text("int main;\n")
            (source / "Kbuild").write_text(
                "rootkitlings-y := core/main.o\n"
            )
            require_compiled_sources(source, ".")

            (source / "network").mkdir()
            (source / "network/agent.c").write_text("int agent;\n")
            with self.assertRaisesRegex(
                SolutionCheckError,
                "Kbuild omits declared C source: network/agent.c",
            ):
                require_compiled_sources(source, ".")

    def test_comments_do_not_satisfy_the_build_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            (source / "unused.c").write_text("int unused;\n")
            (source / "Kbuild").write_text("# unused.o\n")
            with self.assertRaisesRegex(
                SolutionCheckError,
                "Kbuild omits declared C source: unused.c",
            ):
                require_compiled_sources(source, ".")

    def test_unrelated_assignments_and_longer_tokens_do_not_count(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            (source / "network").mkdir()
            (source / "network/agent.c").write_text("int agent;\n")
            impostors = (
                "audit_note := network/agent.o\n",
                "rootkitlings-y := network/agent.o.extra\n",
                "rootkitlings-y := prefix-network/agent.o\n",
            )
            for kbuild in impostors:
                with self.subTest(kbuild=kbuild):
                    (source / "Kbuild").write_text(kbuild)
                    with self.assertRaisesRegex(
                        SolutionCheckError,
                        "Kbuild omits declared C source: network/agent.c",
                    ):
                        require_compiled_sources(source, ".")


if __name__ == "__main__":
    unittest.main()
