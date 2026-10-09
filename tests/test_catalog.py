from pathlib import Path
import json
import subprocess
import tempfile
import tomllib
import unittest

from engine.catalog import (
    Catalog, CatalogError, ElectiveTrack, Exercise, Resources, Verification,
    VerificationCase, Workspace, _elective_track_rows,
)
from engine.workspace import WorkspaceError


ROOT = Path(__file__).resolve().parent.parent


class GeneratedSerialTests(unittest.TestCase):
    def setUp(self) -> None:
        self.catalog = Catalog(ROOT)

    def test_course_does_not_store_aliases(self) -> None:
        course = tomllib.loads((ROOT / "course.toml").read_text(encoding="utf-8"))
        self.assertNotIn("aliases", course)

    def test_unused_capability_registry_is_empty(self) -> None:
        self.assertEqual(self.catalog.capabilities, ())

    def test_serials_follow_route_and_kind(self) -> None:
        prefixes = {"lesson": "L", "milestone": "M", "capstone": "C"}
        counters = {kind: 0 for kind in prefixes}
        for stable_id in self.catalog.required_route:
            exercise = self.catalog.by_id[stable_id]
            expected = f"{prefixes[exercise.kind]}{counters[exercise.kind]:02d}"
            self.assertEqual(expected, exercise.serial)
            self.assertEqual(exercise, self.catalog.resolve(expected))
            counters[exercise.kind] += 1

    def test_required_bundle_paths_match_derived_serials(self) -> None:
        for exercise in self.catalog.exercises:
            self.assertEqual(
                Path("curriculum") / f"{exercise.serial}-{exercise.id}",
                exercise.path,
            )

    def test_required_bundle_path_rejects_wrong_serial_or_nesting(self) -> None:
        exercise = self.catalog.resolve("L00")
        self.catalog._validate_bundle_path(exercise)
        for path in (
            Path("curriculum/L01-module-build-lifecycle"),
            Path("curriculum/nested/L00-module-build-lifecycle"),
        ):
            with self.subTest(path=path):
                misplaced = Exercise(**{**exercise.__dict__, "path": path})
                with self.assertRaisesRegex(
                    CatalogError,
                    "required bundle path must be",
                ):
                    self.catalog._validate_bundle_path(misplaced)

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
        self.assertEqual("module-build-lifecycle", self.catalog.resolve("l00").id)
        self.assertEqual(
            "module-build-lifecycle",
            self.catalog.resolve("module-build").id,
        )

    def test_unique_capstone_prefix_resolves_c00(self) -> None:
        self.assertEqual("authenticated-resident-agent", self.catalog.resolve("C0").id)

    def test_catalog_query_and_export_resolve_default_module_artifact(self) -> None:
        query = subprocess.run(
            [str(ROOT / "scripts/catalog"), "query", "L00", "module_artifact"],
            cwd=ROOT,
            check=True,
            text=True,
            capture_output=True,
        )
        self.assertEqual(query.stdout, "rootkitlings.ko\n")
        exported = subprocess.run(
            [str(ROOT / "scripts/catalog"), "export"],
            cwd=ROOT,
            check=True,
            text=True,
            capture_output=True,
        )
        data = json.loads(exported.stdout)
        self.assertEqual(data["default_module_artifact"], "rootkitlings.ko")
        self.assertEqual(
            data["exercises"][0]["workspace"]["module_artifact"],
            "rootkitlings.ko",
        )
        self.assertIsNone(
            data["exercises"][0]["workspace"]["module_artifact_override"]
        )
        self.assertEqual(data["elective_tracks"], [])
        self.assertEqual(
            data["exercises"][0]["verification"]["cases"],
            [{
                "id": "normal-lifecycle",
                "label": "Normal load and unload",
                "build_variables": {"ROOTKITLINGS_FAIL_POINT": "0"},
                "expect_build": "pass",
                "check": data["exercises"][0]["resources"]["check"],
                "build_output_contains": "",
                "check_arguments": [],
            }],
        )
        self.assertEqual(data["cumulative_root"], "proc-publication")


class ElectiveTrackTests(unittest.TestCase):
    def exercise(self, stable_id: str, status: str) -> Exercise:
        item = _exercise(stable_id, "template")
        return Exercise(**{**item.__dict__, "status": status})

    def catalog(
        self,
        exercises: tuple[Exercise, ...],
        tracks: tuple[ElectiveTrack, ...],
    ) -> Catalog:
        catalog = Catalog.__new__(Catalog)
        catalog.exercises = list(exercises)
        catalog.by_id = {item.id: item for item in exercises}
        catalog.elective_tracks = catalog._generate_track_aliases(tracks)
        catalog.track_aliases = {
            track.serial: track.id for track in catalog.elective_tracks
        }
        return catalog

    def test_absent_registry_is_an_empty_backward_compatible_value(self) -> None:
        self.assertEqual(_elective_track_rows({}), [])
        self.assertEqual(Catalog(ROOT).elective_tracks, ())

    def test_track_aliases_come_only_from_registry_order(self) -> None:
        tracks = (
            ElectiveTrack("debugging-track", "Kernel debugging", ("debugging",)),
            ElectiveTrack("abi-track", "Binary control ABI", ("ioctl",)),
        )
        catalog = self.catalog((), tracks)
        self.assertEqual(
            [(track.serial, track.position) for track in catalog.elective_tracks],
            [("E01", 0), ("E02", 1)],
        )
        self.assertEqual(
            catalog.track_aliases,
            {"E01": "debugging-track", "E02": "abi-track"},
        )

    def test_parser_rejects_empty_and_duplicate_members(self) -> None:
        for members, message in (([], "must not be empty"), (["one", "one"], "duplicate")):
            with self.subTest(members=members):
                with self.assertRaisesRegex(CatalogError, message):
                    _elective_track_rows({
                        "elective_tracks": [{
                            "id": "debugging-track",
                            "label": "Kernel debugging",
                            "members": members,
                        }]
                    })

    def test_each_elective_belongs_to_exactly_one_track(self) -> None:
        required = self.exercise("required", "required")
        elective = self.exercise("debugging", "elective")
        track = ElectiveTrack(
            "debugging-track", "Kernel debugging", ("debugging",)
        )
        catalog = self.catalog((required, elective), (track,))
        catalog._validate_elective_tracks(set(catalog.by_id))

        catalog = self.catalog((required, elective), ())
        with self.assertRaisesRegex(CatalogError, "membership mismatch"):
            catalog._validate_elective_tracks(set(catalog.by_id))

        duplicate = ElectiveTrack(
            "second-track", "Second track", ("debugging",)
        )
        catalog = self.catalog((required, elective), (track, duplicate))
        with self.assertRaisesRegex(CatalogError, "only one"):
            catalog._validate_elective_tracks(set(catalog.by_id))

    def test_track_members_must_exist_and_be_elective(self) -> None:
        required = self.exercise("required", "required")
        wrong_status = ElectiveTrack(
            "wrong-track", "Wrong status", ("required",)
        )
        catalog = self.catalog((required,), (wrong_status,))
        with self.assertRaisesRegex(CatalogError, "status='elective'"):
            catalog._validate_elective_tracks(set(catalog.by_id))

        unknown = ElectiveTrack("unknown-track", "Unknown", ("missing",))
        catalog = self.catalog((required,), (unknown,))
        with self.assertRaisesRegex(CatalogError, "unknown exercise"):
            catalog._validate_elective_tracks(set(catalog.by_id))


class VerificationCaseTests(unittest.TestCase):
    def parse(self, cases):
        with tempfile.TemporaryDirectory() as directory:
            catalog = Catalog.__new__(Catalog)
            catalog.root = Path(directory)
            for case in cases:
                check = case.get("check", "")
                if check:
                    target = catalog.root / check
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_text("#!/bin/sh\n")
            return catalog._verification({
                "timeout_seconds": 30,
                "capabilities": [],
                "tools": [],
                "cleanup_required": True,
                "cases": cases,
            }, Path("exercise.toml"))

    def test_explicit_success_and_failure_cases(self) -> None:
        verification = self.parse([
            {
                "id": "normal", "label": "Normal load",
                "build_variables": {"RK_FAIL_POINT": "0"},
                "expect_build": "pass", "check": "checks/normal",
                "build_output_contains": "",
                "check_arguments": ["125", "Preflight failure"],
            },
            {
                "id": "unknown", "label": "Unknown value",
                "build_variables": {"RK_FAIL_POINT": "99"},
                "expect_build": "fail", "check": "",
                "build_output_contains": "unsupported failure point",
            },
        ])
        self.assertEqual(verification.cases[0].build_variables, (("RK_FAIL_POINT", "0"),))
        self.assertEqual(
            verification.cases[0].check_arguments,
            ("125", "Preflight failure"),
        )
        self.assertEqual(verification.cases[1].expect_build, "fail")

    def test_implicit_default_case_preserves_existing_manifests(self) -> None:
        exercise = _exercise("existing", "template")
        catalog = Catalog.__new__(Catalog)
        self.assertEqual(
            catalog.verification_cases(exercise),
            (VerificationCase("default", "Default", (), "pass", "check", ""),),
        )

    def test_rejects_duplicate_ids_and_labels(self) -> None:
        base = {
            "id": "normal", "label": "Normal",
            "build_variables": {}, "expect_build": "pass",
            "check": "check", "build_output_contains": "",
        }
        for duplicate in (
            {**base, "label": "Other"},
            {**base, "id": "other", "label": "normal"},
        ):
            with self.subTest(duplicate=duplicate):
                with self.assertRaisesRegex(CatalogError, "duplicate verification case"):
                    self.parse([base, duplicate])

    def test_rejects_unsafe_variables_and_unprintable_or_long_values(self) -> None:
        base = {
            "id": "normal", "label": "Normal", "expect_build": "pass",
            "check": "check", "build_output_contains": "",
        }
        for variables in ({"BAD-NAME": "1"}, {"GOOD": "line\nbreak"}, {"GOOD": "x" * 257}):
            with self.subTest(variables=variables):
                with self.assertRaises(CatalogError):
                    self.parse([{**base, "build_variables": variables}])

    def test_rejects_invalid_guest_check_arguments(self) -> None:
        base = {
            "id": "normal", "label": "Normal", "build_variables": {},
            "expect_build": "pass", "check": "check",
            "build_output_contains": "",
        }
        for arguments in (
            "not-an-array",
            ["line\nbreak"],
            ["x" * 257],
            [str(index) for index in range(17)],
        ):
            with self.subTest(arguments=arguments):
                with self.assertRaises(CatalogError):
                    self.parse([{**base, "check_arguments": arguments}])

    def test_enforces_success_and_failure_contracts(self) -> None:
        cases = (
            {
                "id": "success", "label": "Success", "build_variables": {},
                "expect_build": "pass", "check": "",
                "build_output_contains": "",
            },
            {
                "id": "success", "label": "Success", "build_variables": {},
                "expect_build": "pass", "check": "check",
                "build_output_contains": "unexpected",
            },
            {
                "id": "failure", "label": "Failure", "build_variables": {},
                "expect_build": "fail", "check": "check",
                "build_output_contains": "error",
            },
        )
        for case in cases:
            with self.subTest(case=case):
                with self.assertRaises(CatalogError):
                    self.parse([case])

        failure = self.parse([
            {
                "id": "control", "label": "Control", "build_variables": {},
                "expect_build": "pass", "check": "check",
                "build_output_contains": "",
            },
            {
                "id": "failure", "label": "Failure", "build_variables": {},
                "expect_build": "fail", "check": "",
                "build_output_contains": "",
            },
        ])
        self.assertEqual(failure.cases[1].build_output_contains, "")

    def test_cases_require_control_before_failures(self) -> None:
        failure = {
            "id": "failure", "label": "Failure", "build_variables": {},
            "expect_build": "fail", "check": "",
            "build_output_contains": "",
        }
        control = {
            "id": "control", "label": "Control", "build_variables": {},
            "expect_build": "pass", "check": "check",
            "build_output_contains": "",
        }
        with self.assertRaisesRegex(CatalogError, "successful control"):
            self.parse([failure])
        with self.assertRaisesRegex(CatalogError, "must precede"):
            self.parse([failure, control])

    def test_catalog_validation_requires_case_check_to_exist(self) -> None:
        catalog = Catalog(ROOT)
        item = catalog.exercises[0]
        verification = Verification(
            item.verification.timeout_seconds,
            item.verification.capabilities,
            item.verification.tools,
            item.verification.cleanup_required,
            (VerificationCase(
                "normal", "Normal", (), "pass", "missing/guest-check", ""
            ),),
        )
        explicit = Exercise(**{**item.__dict__, "verification": verification})
        with self.assertRaisesRegex(CatalogError, "missing verification case check"):
            catalog._validate_paths(explicit)


def _exercise(
    stable_id: str,
    baseline: str,
    *,
    kind: str = "lesson",
    files: tuple[str, ...] = ("Kbuild", "main.c"),
    build: str = ".",
    additions: str = "",
    module_artifact: str = "",
) -> Exercise:
    workspace = Workspace(
        mode="cumulative",
        source="template" if baseline in {"template", "scaffold"} else "",
        files=files,
        editable=("main.c",),
        build=build,
        baseline=baseline,
        parent="",
        additions=additions,
        module_artifact=module_artifact,
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
        catalog.default_module_artifact = "hello.ko"
        catalog.cumulative_root = ""
        return catalog

    def test_cumulative_root_resolves_the_declared_project_lesson(self) -> None:
        root = _exercise("project-root", "template")
        practice = _exercise("practice", "template")
        first = _exercise("first-milestone", "cumulative-root", kind="milestone")
        catalog = self.catalog((root, practice, first))
        catalog.cumulative_root = root.id
        self.assertEqual(catalog.baseline_provider(first), root)
        self.assertEqual(catalog.completion_dependencies(first), (root.id,))

    def test_declared_cumulative_route_has_one_root_and_fixed_milestone_chain(self) -> None:
        root = _exercise("project-root", "template")
        milestones = (
            _exercise("m0", "cumulative-root", kind="milestone"),
            _exercise("m1", "previous-milestone", kind="milestone"),
            _exercise("m2", "previous-milestone", kind="milestone"),
            _exercise("m3", "previous-milestone", kind="milestone"),
        )
        capstone = _exercise("final", "previous-milestone", kind="capstone")
        catalog = self.catalog((root, *milestones, capstone))
        catalog.cumulative_root = root.id
        catalog._validate_cumulative_route()

        wrong = Exercise(**{
            **milestones[0].__dict__,
            "workspace": Workspace(**{
                **milestones[0].workspace.__dict__,
                "baseline": "previous-required",
            }),
        })
        catalog = self.catalog((root, wrong, *milestones[1:], capstone))
        catalog.cumulative_root = root.id
        with self.assertRaisesRegex(CatalogError, "first milestone"):
            catalog._validate_cumulative_route()

    def test_previous_required_is_immediate_required_route_item(self) -> None:
        first = _exercise("first", "template")
        elective = _exercise("elective", "template")
        second = _exercise("second", "previous-required")
        catalog = self.catalog((first, second), (elective,))
        self.assertEqual(catalog.baseline_provider(second), first)
        self.assertEqual(catalog.completion_dependencies(second), ("first",))

    def test_scaffold_reuses_reference_without_a_completion_edge(self) -> None:
        provider = _exercise("provider", "template")
        consumer = _exercise(
            "consumer",
            "scaffold",
            files=("Kbuild", "main.c", "new.c", "supplied.c"),
        )
        consumer = Exercise(**{
            **consumer.__dict__,
            "workspace": Workspace(**{
                **consumer.workspace.__dict__,
                "parent": provider.id,
                "source": "consumer-template",
            }),
            "resources": Resources("", "", "", "", "consumer-solution"),
        })
        provider = Exercise(**{
            **provider.__dict__,
            "resources": Resources("", "", "", "", "provider-solution"),
        })
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for tree, files in {
                "provider-solution": {
                    "Kbuild": "obj-m += rootkitlings.o\n",
                    "main.c": "int inherited;\n",
                },
                "consumer-template": {
                    "main.c": "int todo;\n",
                    "new.c": "int new_todo;\n",
                    "supplied.c": "int supplied;\n",
                },
                "consumer-solution": {
                    "main.c": "int solved;\n",
                    "new.c": "int new_solved;\n",
                },
            }.items():
                target = root / tree
                target.mkdir()
                for name, content in files.items():
                    (target / name).write_text(content)

            catalog = self.catalog((provider, consumer))
            catalog.root = root
            self.assertIsNone(catalog.baseline_provider(consumer))
            self.assertEqual(catalog.scaffold_provider(consumer), provider)
            self.assertEqual(catalog.completion_dependencies(consumer), ())
            catalog.validate_scaffold_lineage(consumer)
            starter = catalog.starter_tree(consumer)
            self.assertEqual(
                starter["main.c"].data,
                b"int todo;\n",
            )
            self.assertEqual(
                catalog.solution_tree(consumer)["new.c"].data,
                b"int new_solved;\n",
            )
            self.assertEqual(
                catalog.solution_tree(consumer)["supplied.c"].data,
                b"int supplied;\n",
            )
            work = root / "work"
            work.mkdir()
            materialized = catalog.materialize_starter(consumer, work)
            self.assertEqual(
                (materialized / "Kbuild").read_text(),
                "obj-m += rootkitlings.o\n",
            )

    def test_direct_prerequisites_reject_duplicates_and_transitive_edges(self) -> None:
        first = _exercise("first", "template")
        second = Exercise(**{
            **_exercise("second", "template").__dict__,
            "prerequisites": ("first",),
        })
        duplicate = Exercise(**{
            **_exercise("duplicate", "template").__dict__,
            "prerequisites": ("second", "second"),
        })
        catalog = self.catalog((first, second, duplicate))
        with self.assertRaisesRegex(CatalogError, "duplicate prerequisites"):
            catalog._validate_direct_prerequisites()

        redundant = Exercise(**{
            **_exercise("redundant", "template").__dict__,
            "prerequisites": ("first", "second"),
        })
        catalog = self.catalog((first, second, redundant))
        with self.assertRaisesRegex(
            CatalogError,
            "prerequisite 'first' is already implied by 'second'",
        ):
            catalog._validate_direct_prerequisites()

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

    def test_solution_composes_supplied_additions_before_sparse_overlay(self) -> None:
        provider = _exercise("provider", "template")
        consumer = _exercise(
            "consumer",
            "previous-required",
            files=("Kbuild", "main.c", "supplied.c"),
            additions="additions",
        )
        provider = Exercise(**{
            **provider.__dict__,
            "resources": Resources("README", "check", "", "hint", "provider"),
        })
        consumer = Exercise(**{
            **consumer.__dict__,
            "resources": Resources("README", "check", "", "hint", "consumer"),
        })
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("provider", "consumer", "additions"):
                (root / name).mkdir()
            (root / "provider/Kbuild").write_text("base build\n")
            (root / "provider/main.c").write_text("base main\n")
            (root / "additions/supplied.c").write_text("supplied\n")
            (root / "consumer/main.c").write_text("completed main\n")

            catalog = self.catalog((provider, consumer))
            catalog.root = root
            tree = catalog.solution_tree(consumer)
            self.assertEqual(tree["supplied.c"].data, b"supplied\n")
            self.assertEqual(tree["main.c"].data, b"completed main\n")

            (root / "consumer/supplied.c").write_text("supplied\n")
            with self.assertRaisesRegex(WorkspaceError, "byte-identical"):
                catalog.solution_tree(consumer)

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

    def test_module_artifact_uses_default_override_and_provider(self) -> None:
        defaulted = _exercise("defaulted", "template")
        overridden = _exercise(
            "overridden",
            "template",
            module_artifact="rootkitlings.ko",
        )
        inherited = _exercise("inherited", "previous-required")
        catalog = self.catalog((overridden, inherited), (defaulted,))
        self.assertEqual(catalog.module_artifact(defaulted), "hello.ko")
        self.assertEqual(catalog.module_artifact(overridden), "rootkitlings.ko")
        self.assertEqual(catalog.module_artifact(inherited), "rootkitlings.ko")

    def test_inherited_module_artifact_cannot_conflict(self) -> None:
        provider = _exercise(
            "provider",
            "template",
            module_artifact="rootkitlings.ko",
        )
        consumer = _exercise(
            "consumer",
            "previous-required",
            module_artifact="other.ko",
        )
        catalog = self.catalog((provider, consumer))
        with self.assertRaisesRegex(CatalogError, "module artifact conflicts"):
            catalog.validate_workspace_lineage(consumer)

    def test_only_explicit_completion_keeps_full_solution_tree(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("provider", "completion", "derived"):
                solution = root / name
                solution.mkdir()
                (solution / "Kbuild").write_text("obj-m += hello.o\n")
                (solution / "main.c").write_text("int value;\n")

            provider = _exercise("provider", "template")
            provider = Exercise(**{
                **provider.__dict__,
                "resources": Resources("", "", "", "", "provider"),
            })
            completion = _exercise("completion", "completion")
            completion = Exercise(**{
                **completion.__dict__,
                "workspace": Workspace(**{
                    **completion.workspace.__dict__,
                    "parent": "provider",
                }),
                "resources": Resources("", "", "", "", "completion"),
            })
            derived = _exercise("derived", "previous-required")
            derived = Exercise(**{
                **derived.__dict__,
                "resources": Resources("", "", "", "", "derived"),
            })

            catalog = self.catalog((provider, completion))
            catalog.root = root
            self.assertEqual(
                set(catalog.solution_tree(completion)),
                {"Kbuild", "main.c"},
            )

            catalog = self.catalog((provider, derived))
            catalog.root = root
            with self.assertRaisesRegex(WorkspaceError, "byte-identical"):
                catalog.solution_tree(derived)


class ModuleArtifactValidationTests(unittest.TestCase):
    def test_rejects_unsafe_or_non_module_names(self) -> None:
        from engine.catalog import _module_artifact

        for value in (
            "hello",
            "../hello.ko",
            "dir/hello.ko",
            "dir\\hello.ko",
            "..hello.ko",
            "-hello.ko",
            "hello ko.ko",
            "",
        ):
            with self.subTest(value=value):
                with self.assertRaisesRegex(CatalogError, "portable .ko basename"):
                    _module_artifact(value, Path("test"))

    def test_accepts_portable_module_names(self) -> None:
        from engine.catalog import _module_artifact

        self.assertEqual(
            _module_artifact("rootkitlings_test-1.ko", Path("test")),
            "rootkitlings_test-1.ko",
        )


if __name__ == "__main__":
    unittest.main()
