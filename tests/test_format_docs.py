from __future__ import annotations

from pathlib import Path
import runpy
import tempfile
import unittest


ROOT = Path(__file__).resolve().parent.parent
FORMAT = runpy.run_path(ROOT / "scripts/format-docs")
format_markdown = FORMAT["format_markdown"]
overlong_prose = FORMAT["overlong_prose"]
course_documents = FORMAT["course_documents"]


class MarkdownFormatTests(unittest.TestCase):
    def test_wraps_prose_and_list_items_to_80_columns(self) -> None:
        source = (
            "A paragraph with enough ordinary words to exceed the configured "
            "width while remaining easy to wrap without changing its "
            "meaning.\n\n"
            "- A list item with enough ordinary words to exceed the configured "
            "width while keeping a readable hanging indent.\n"
        )
        formatted = format_markdown(source)
        self.assertTrue(all(len(line) <= 80 for line in formatted.splitlines()))
        self.assertIn("\n  keeping a readable hanging indent", formatted)

    def test_preserves_fences_tables_and_long_links(self) -> None:
        source = (
            "```c\n"
            "this literal line is deliberately wider than eighty columns and "
            "must remain exactly unchanged by the prose formatter;\n"
            "```\n\n"
            "| Offset | Field |\n"
            "|---:|---|\n\n"
            "Read [the exact primary source](https://example.com/a/very/long/"
            "destination/that/must/not/be/split/across/source-lines).\n"
        )
        formatted = format_markdown(source)
        self.assertIn(
            "this literal line is deliberately wider than eighty columns and "
            "must remain exactly unchanged by the prose formatter;",
            formatted,
        )
        self.assertIn("| Offset | Field |\n|---:|---|", formatted)
        self.assertIn(
            "https://example.com/a/very/long/destination/that/must/not/be/"
            "split/across/source-lines",
            formatted,
        )

    def test_is_idempotent(self) -> None:
        source = "Short prose stays stable.\n"
        once = format_markdown(source)
        self.assertEqual(format_markdown(once), once)

    def test_starts_each_sentence_on_a_new_source_line(self) -> None:
        source = "First sentence. Second sentence.\n"
        self.assertEqual(
            format_markdown(source),
            "First sentence.\nSecond sentence.\n",
        )

    def test_indents_a_reference_destination_on_the_next_line(self) -> None:
        source = "[1]:\nhttps://example.com/a/long/source\n"
        self.assertEqual(
            format_markdown(source),
            "[1]:\n  https://example.com/a/long/source\n",
        )

    def test_width_check_allows_literal_blocks_and_exact_urls(self) -> None:
        source = (
            "```text\n"
            + "x" * 90
            + "\n```\n"
            + "https://example.com/"
            + "x" * 90
            + "\n"
            + "prose "
            + "x" * 90
            + "\n"
        )
        self.assertEqual(overlong_prose(source), [5])

    def test_document_discovery_excludes_runtime_and_learner_files(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            maintained = root / "docs/guide.md"
            learner = root / "work/L00/notes.md"
            runtime = root / ".rklings/log.md"
            hint = root / "curriculum/L00-example/hint"
            for path in (maintained, learner, runtime, hint):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("text\n", encoding="utf-8")

            self.assertEqual(set(course_documents(root)), {maintained, hint})


if __name__ == "__main__":
    unittest.main()
