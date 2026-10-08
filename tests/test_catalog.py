from pathlib import Path
import tempfile
import tomllib
import unittest

from lib.catalog import Catalog, CatalogError, Exercise, Resources, Verification, Workspace


ROOT = Path(__file__).resolve().parent.parent


class GeneratedSerialTests(unittest.TestCase):
    def setUp(self) -> None:
        self.catalog = Catalog(ROOT)

    def test_course_does_not_store_aliases(self) -> None:
        course = tomllib.loads((ROOT / "course.toml").read_text(encoding="utf-8"))
        self.assertNotIn("aliases", course)

    def test_serials_follow_route_and_kind(self) -> None:
        prefixes = {"lesson": "L", "milestone": "M", "capstone": "C"}
        counters = {kind: 0 for kind in prefixes}
        for stable_id in self.catalog.required_route:
            exercise = self.catalog.by_id[stable_id]
            expected = f"{prefixes[exercise.kind]}{counters[exercise.kind]:02d}"
            self.assertEqual(expected, exercise.serial)
            self.assertEqual(exercise, self.catalog.resolve(expected))
            counters[exercise.kind] += 1

    def test_discovery_order_does_not_define_serials(self) -> None:
        route_positions = {
            stable_id: position
            for position, stable_id in enumerate(self.catalog.required_route)
        }
        ordered = sorted(
            self.catalog.exercises,
            key=lambda exercise: route_positions.get(exercise.id, len(route_positions)),
        )
        required = [exercise for exercise in ordered if exercise.status == "required"]
        self.assertEqual(list(self.catalog.required_route), [item.id for item in required])

    def test_selectors_accept_exact_aliases_and_unique_prefixes(self) -> None:
        self.assertEqual("module-lifecycle", self.catalog.resolve("l00").id)
        self.assertEqual("module-lifecycle", self.catalog.resolve("module-lif").id)

    def test_ambiguous_prefix_lists_matches(self) -> None:
        with self.assertRaisesRegex(ValueError, r"ambiguous exercise 'C0'.*C00.*C09"):
            self.catalog.resolve("C0")


def _exercise(
    stable_id: str,
    baseline: str,
    *,
    kind: str = "lesson",
    files: tuple[str, ...] = ("Kbuild", "main.c"),
    build: str = ".",
    additions: str = "",
) -> Exercise:
    workspace = Workspace(
        mode="cumulative",
        source="template" if baseline == "template" else "",
        files=files,
        editable=("main.c",),
        build=build,
        baseline=baseline,
        parent="",
        additions=additions,
    )
    resources = Resources("README", "check", "", "hint", "solution")
    verification = Verification(30, (), (), True)
    return Exercise(
        id=stable_id,
        path=Path(stable_id),
        title=stable_id,
        stage="stage",
        kind=kind,
        status="required",
        tradecraft=(),
        difficulty="standard",
        prerequisites=(),
        teaches=(f"concept-{stable_id}",),
        requires=(),
        workspace=workspace,
        resources=resources,
        verification=verification,
    )


class DerivedBaselineTests(unittest.TestCase):
    def catalog(self, route: tuple[Exercise, ...], extras=()) -> Catalog:
        catalog = Catalog.__new__(Catalog)
        catalog.required_route = tuple(item.id for item in route)
        catalog.exercises = list(route) + list(extras)
        catalog.by_id = {item.id: item for item in catalog.exercises}
        return catalog

    def test_previous_required_is_immediate_required_route_item(self) -> None:
        first = _exercise("first", "template")
        elective = _exercise("elective", "template")
        second = _exercise("second", "previous-required")
        catalog = self.catalog((first, second), (elective,))
        self.assertEqual(catalog.baseline_provider(second), first)
        self.assertEqual(catalog.completion_dependencies(second), ("first",))

    def test_previous_milestone_ignores_intervening_lessons_and_electives(self) -> None:
        milestone = _exercise("milestone", "template", kind="milestone")
        lesson = _exercise("lesson", "template")
        elective_milestone = _exercise(
            "elective-milestone",
            "template",
            kind="milestone",
        )
        later = _exercise("later", "previous-milestone", kind="milestone")
        catalog = self.catalog(
            (milestone, lesson, later),
            (elective_milestone,),
        )
        self.assertEqual(catalog.baseline_provider(later), milestone)

    def test_missing_derived_provider_is_rejected(self) -> None:
        first = _exercise("first", "previous-required")
        catalog = self.catalog((first,))
        with self.assertRaisesRegex(CatalogError, "no previous required"):
            catalog.baseline_provider(first)

        lesson = _exercise("lesson", "template")
        milestone = _exercise("milestone", "previous-milestone", kind="milestone")
        catalog = self.catalog((lesson, milestone))
        with self.assertRaisesRegex(CatalogError, "no previous milestone"):
            catalog.baseline_provider(milestone)

        lesson = _exercise("consumer", "previous-milestone")
        catalog = self.catalog((
            _exercise("earlier", "template", kind="milestone"),
            lesson,
        ))
        with self.assertRaisesRegex(CatalogError, "milestone or capstone kind"):
            catalog.baseline_provider(lesson)

        required = _exercise("required", "template")
        elective = _exercise("elective", "previous-required")
        catalog = self.catalog((required,), (elective,))
        with self.assertRaisesRegex(CatalogError, "only valid on the required route"):
            catalog.baseline_provider(elective)

    def test_inherited_mode_and_build_root_must_match(self) -> None:
        provider = _exercise("provider", "template")
        incompatible_files = _exercise(
            "files",
            "previous-required",
            files=("Kbuild", "other.c"),
        )
        catalog = self.catalog((provider, incompatible_files))
        with self.assertRaisesRegex(CatalogError, "cannot remove or rename"):
            catalog.validate_workspace_lineage(incompatible_files)

        incompatible_build = _exercise("build", "previous-required", build="src")
        catalog = self.catalog((provider, incompatible_build))
        with self.assertRaisesRegex(CatalogError, "mode/build must match"):
            catalog.validate_workspace_lineage(incompatible_build)

        different_mode_provider = _exercise("mode-provider", "template")
        different_mode_provider = Exercise(
            **{
                **different_mode_provider.__dict__,
                "workspace": Workspace(
                    **{
                        **different_mode_provider.workspace.__dict__,
                        "mode": "project",
                    }
                ),
            }
        )
        consumer = _exercise("mode-consumer", "previous-required")
        catalog = self.catalog((different_mode_provider, consumer))
        with self.assertRaisesRegex(CatalogError, "mode/build must match"):
            catalog.validate_workspace_lineage(consumer)

    def test_additive_inheritance_requires_exact_additions_tree(self) -> None:
        provider = _exercise("provider", "template")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            additions = root / "additions"
            additions.mkdir()
            (additions / "new.c").write_text("int new_value;\n")
            consumer = _exercise(
                "consumer",
                "previous-required",
                files=("Kbuild", "main.c", "new.c"),
                additions="additions",
            )
            catalog = self.catalog((provider, consumer))
            catalog.root = root
            catalog.validate_workspace_lineage(consumer)
            self.assertEqual(
                consumer.value("workspace_additions"),
                "additions",
            )

            (additions / "extra.c").write_text("unexpected\n")
            with self.assertRaisesRegex(CatalogError, "additions directory mismatch"):
                catalog.validate_workspace_lineage(consumer)

            (additions / "extra.c").unlink()
            (additions / "empty").mkdir()
            with self.assertRaisesRegex(CatalogError, "stray or empty"):
                catalog.validate_workspace_lineage(consumer)

    def test_additive_inheritance_rejects_missing_additions_directory(self) -> None:
        provider = _exercise("provider", "template")
        consumer = _exercise(
            "consumer",
            "previous-required",
            files=("Kbuild", "main.c", "new.c"),
            additions="missing",
        )
        catalog = self.catalog((provider, consumer))
        with tempfile.TemporaryDirectory() as directory:
            catalog.root = Path(directory)
            with self.assertRaisesRegex(CatalogError, "existing real directory"):
                catalog.validate_workspace_lineage(consumer)

    def test_additions_presence_matches_actual_file_delta(self) -> None:
        provider = _exercise("provider", "template")
        missing = _exercise(
            "missing",
            "previous-required",
            files=("Kbuild", "main.c", "new.c"),
        )
        catalog = self.catalog((provider, missing))
        with self.assertRaisesRegex(CatalogError, "no additions directory"):
            catalog.validate_workspace_lineage(missing)

        unnecessary = _exercise(
            "unnecessary",
            "previous-required",
            additions="additions",
        )
        catalog = self.catalog((provider, unnecessary))
        with self.assertRaisesRegex(CatalogError, "forbidden when no files"):
            catalog.validate_workspace_lineage(unnecessary)


if __name__ == "__main__":
    unittest.main()
