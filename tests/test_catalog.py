from pathlib import Path
import json
import subprocess
import tempfile
import tomllib
import unittest

from engine.catalog import (
    Catalog, CatalogError, ElectiveTrack, Exercise, Resources, Verification,
    VerificationCase, Workspace, WorkspaceImport, _elective_track_rows,
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
        prefixes = {"lesson": "L", "milestone": "M", "final-project": "C"}
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

    def test_unique_final_project_prefix_resolves_c00(self) -> None:
        self.assertEqual("complete-rootkit", self.catalog.resolve("C0").id)

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
        self.assertNotIn("cumulative_root", data)


class ElectiveTrackTests(unittest.TestCase):
    def exercise(self, stable_id: str, status: str) -> Exercise:
        item = _exercise(stable_id, "starter")
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
        exercise = _exercise("existing", "starter")
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
    parent: str = "",
) -> Exercise:
    workspace = Workspace(
        mode="cumulative",
        source="starter" if baseline in {"starter", "scaffold"} else "",
        files=files,
        editable=("main.c",),
        build=build,
        baseline=baseline,
        parent=parent,
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


class WorkspaceInheritanceTests(unittest.TestCase):
    def catalog(self, route: tuple[Exercise, ...], extras=()) -> Catalog:
        catalog = Catalog.__new__(Catalog)
        catalog.required_route = tuple(item.id for item in route)
        catalog.exercises = list(route) + list(extras)
        catalog.by_id = {item.id: item for item in catalog.exercises}
        catalog.default_module_artifact = "hello.ko"
        return catalog

    def test_only_explicit_workspace_baselines_are_accepted(self) -> None:
        catalog = Catalog.__new__(Catalog)
        catalog.root = ROOT
        workspace = {
            "mode": "cumulative",
            "files": ["Kbuild", "main.c"],
            "editable": ["main.c"],
            "build": ".",
        }
        for baseline in (
            "template",
            "completion",
            "cumulative-root",
            "previous-required",
            "previous-milestone",
        ):
            with self.subTest(baseline=baseline):
                with self.assertRaisesRegex(
                    CatalogError, "invalid workspace baseline"
                ):
                    catalog._workspace(
                        {**workspace, "baseline": baseline}, Path("exercise.toml")
                    )

    def test_workspace_import_schema_selects_and_optionally_renames_files(self) -> None:
        catalog = Catalog.__new__(Catalog)
        catalog.root = ROOT
        workspace = catalog._workspace({
            "mode": "project",
            "files": ["main.c", "vendor/api.h", "shared.c"],
            "editable": ["main.c"],
            "build": ".",
            "baseline": "starter",
            "source": "starter",
            "imports": [{
                "exercise": "provider",
                "files": ["include/api.h", "shared.c"],
                "rename": {"include/api.h": "vendor/api.h"},
            }],
        }, Path("exercise.toml"))

        self.assertEqual(workspace.imports, (
            WorkspaceImport("provider", (
                ("include/api.h", "vendor/api.h"),
                ("shared.c", "shared.c"),
            )),
        ))

    def test_workspace_import_schema_rejects_ambiguous_rows(self) -> None:
        catalog = Catalog.__new__(Catalog)
        catalog.root = ROOT
        common = {
            "mode": "project",
            "files": ["main.c"],
            "editable": ["main.c"],
            "build": ".",
            "baseline": "starter",
            "source": "starter",
        }
        with self.assertRaisesRegex(CatalogError, "unselected source"):
            catalog._workspace({
                **common,
                "imports": [{
                    "exercise": "provider",
                    "files": ["main.c"],
                    "rename": {"other.c": "copy.c"},
                }],
            }, Path("exercise.toml"))
        with self.assertRaisesRegex(CatalogError, "duplicate import exercise"):
            catalog._workspace({
                **common,
                "imports": [
                    {"exercise": "provider", "files": ["a.c"]},
                    {"exercise": "provider", "files": ["b.c"]},
                ],
            }, Path("exercise.toml"))

    def test_parent_baseline_resolves_its_explicit_parent(self) -> None:
        first = _exercise("first", "starter")
        intervening = _exercise("intervening", "starter")
        second = _exercise("second", "parent", parent="first")
        catalog = self.catalog((first, intervening, second))
        self.assertEqual(catalog.baseline_provider(second), first)
        self.assertEqual(catalog.completion_dependencies(second), ("first",))

    def test_workspace_parent_must_be_earlier_on_required_route(self) -> None:
        first = _exercise("first", "parent", parent="second")
        second = _exercise("second", "starter")
        catalog = self.catalog((first, second))
        with self.assertRaisesRegex(CatalogError, "parent must be earlier"):
            catalog._validate_required_route()

        elective = Exercise(**{
            **_exercise("elective", "starter").__dict__,
            "status": "elective",
        })
        consumer = _exercise("consumer", "parent", parent="elective")
        catalog = self.catalog((consumer,), (elective,))
        with self.assertRaisesRegex(CatalogError, "parent must be earlier"):
            catalog._validate_required_route()

    def test_required_exercise_cannot_depend_on_non_required_exercise(self) -> None:
        elective = Exercise(**{
            **_exercise("elective", "starter").__dict__,
            "status": "elective",
        })
        required = Exercise(**{
            **_exercise("required", "starter").__dict__,
            "prerequisites": ("elective",),
        })
        catalog = self.catalog((required,), (elective,))
        with self.assertRaisesRegex(CatalogError, "cannot depend on non-required"):
            catalog._validate_required_route()

    def test_final_project_is_optional_unique_and_final(self) -> None:
        lessons = tuple(
            _exercise(f"lesson-{index}", "starter") for index in range(3)
        )
        milestones = tuple(
            _exercise(f"milestone-{index}", "starter", kind="milestone")
            for index in range(6)
        )
        self.catalog((*lessons, *milestones))._validate_required_route()

        final_project = _exercise(
            "final-project", "starter", kind="final-project"
        )
        self.catalog(
            (*lessons, *milestones, final_project)
        )._validate_required_route()

        trailing = _exercise("trailing", "starter")
        with self.assertRaisesRegex(CatalogError, "end the required route"):
            self.catalog(
                (*lessons, final_project, trailing)
            )._validate_required_route()

        other = _exercise("other-project", "starter", kind="final-project")
        with self.assertRaisesRegex(CatalogError, "final project must be unique"):
            self.catalog(
                (*lessons, final_project, other)
            )._validate_required_route()

    def test_scaffold_reuses_reference_without_a_completion_edge(self) -> None:
        provider = _exercise("provider", "starter")
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
                "source": "consumer-starter",
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
                "consumer-starter": {
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
        first = _exercise("first", "starter")
        second = Exercise(**{
            **_exercise("second", "starter").__dict__,
            "prerequisites": ("first",),
        })
        duplicate = Exercise(**{
            **_exercise("duplicate", "starter").__dict__,
            "prerequisites": ("second", "second"),
        })
        catalog = self.catalog((first, second, duplicate))
        with self.assertRaisesRegex(CatalogError, "duplicate prerequisites"):
            catalog._validate_direct_prerequisites()

        redundant = Exercise(**{
            **_exercise("redundant", "starter").__dict__,
            "prerequisites": ("first", "second"),
        })
        catalog = self.catalog((first, second, redundant))
        with self.assertRaisesRegex(
            CatalogError,
            "prerequisite 'first' is already implied by 'second'",
        ):
            catalog._validate_direct_prerequisites()

    def test_unknown_workspace_parent_is_rejected(self) -> None:
        consumer = _exercise("consumer", "parent", parent="missing")
        catalog = self.catalog((consumer,))
        with self.assertRaisesRegex(CatalogError, "unknown workspace parent"):
            catalog.baseline_provider(consumer)

    def test_inherited_mode_and_build_root_must_match(self) -> None:
        provider = _exercise("provider", "starter")
        incompatible_files = _exercise(
            "files",
            "parent",
            files=("Kbuild", "other.c"),
            parent="provider",
        )
        catalog = self.catalog((provider, incompatible_files))
        with self.assertRaisesRegex(CatalogError, "cannot remove or rename"):
            catalog.validate_workspace_lineage(incompatible_files)

        incompatible_build = _exercise(
            "build", "parent", build="src", parent="provider"
        )
        catalog = self.catalog((provider, incompatible_build))
        with self.assertRaisesRegex(CatalogError, "mode/build must match"):
            catalog.validate_workspace_lineage(incompatible_build)

        different_mode_provider = _exercise("mode-provider", "starter")
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
        consumer = _exercise(
            "mode-consumer", "parent", parent="mode-provider"
        )
        catalog = self.catalog((different_mode_provider, consumer))
        with self.assertRaisesRegex(CatalogError, "mode/build must match"):
            catalog.validate_workspace_lineage(consumer)

    def test_additive_inheritance_requires_exact_additions_tree(self) -> None:
        provider = _exercise("provider", "starter")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            additions = root / "additions"
            additions.mkdir()
            (additions / "new.c").write_text("int new_value;\n")
            consumer = _exercise(
                "consumer",
                "parent",
                files=("Kbuild", "main.c", "new.c"),
                additions="additions",
                parent="provider",
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

    def test_parent_solution_composes_additions_before_sparse_overlay(self) -> None:
        provider = _exercise("provider", "starter")
        consumer = _exercise(
            "consumer",
            "parent",
            files=("Kbuild", "main.c", "supplied.c"),
            additions="additions",
            parent="provider",
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
            self.assertEqual(tree["Kbuild"].data, b"base build\n")
            self.assertEqual(tree["main.c"].data, b"completed main\n")
            self.assertEqual(tree["supplied.c"].data, b"supplied\n")

    def test_additive_inheritance_rejects_missing_additions_directory(self) -> None:
        provider = _exercise("provider", "starter")
        consumer = _exercise(
            "consumer",
            "parent",
            files=("Kbuild", "main.c", "new.c"),
            additions="missing",
            parent="provider",
        )
        catalog = self.catalog((provider, consumer))
        with tempfile.TemporaryDirectory() as directory:
            catalog.root = Path(directory)
            with self.assertRaisesRegex(CatalogError, "existing real directory"):
                catalog.validate_workspace_lineage(consumer)

    def test_additions_presence_matches_actual_file_delta(self) -> None:
        provider = _exercise("provider", "starter")
        missing = _exercise(
            "missing",
            "parent",
            files=("Kbuild", "main.c", "new.c"),
            parent="provider",
        )
        catalog = self.catalog((provider, missing))
        with self.assertRaisesRegex(CatalogError, "no additions directory"):
            catalog.validate_workspace_lineage(missing)

        unnecessary = _exercise(
            "unnecessary",
            "parent",
            additions="additions",
            parent="provider",
        )
        catalog = self.catalog((provider, unnecessary))
        with self.assertRaisesRegex(CatalogError, "forbidden when no files"):
            catalog.validate_workspace_lineage(unnecessary)

    def test_module_artifact_uses_default_override_and_provider(self) -> None:
        defaulted = _exercise("defaulted", "starter")
        overridden = _exercise(
            "overridden",
            "starter",
            module_artifact="rootkitlings.ko",
        )
        inherited = _exercise(
            "inherited", "parent", parent="overridden"
        )
        catalog = self.catalog((overridden, inherited), (defaulted,))
        self.assertEqual(catalog.module_artifact(defaulted), "hello.ko")
        self.assertEqual(catalog.module_artifact(overridden), "rootkitlings.ko")
        self.assertEqual(catalog.module_artifact(inherited), "rootkitlings.ko")

    def test_inherited_module_artifact_cannot_conflict(self) -> None:
        provider = _exercise(
            "provider",
            "starter",
            module_artifact="rootkitlings.ko",
        )
        consumer = _exercise(
            "consumer",
            "parent",
            module_artifact="other.ko",
            parent="provider",
        )
        catalog = self.catalog((provider, consumer))
        with self.assertRaisesRegex(CatalogError, "module artifact conflicts"):
            catalog.validate_workspace_lineage(consumer)

    def test_parent_solution_rejects_redundant_sparse_files(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("provider", "consumer"):
                solution = root / name
                solution.mkdir()
                (solution / "Kbuild").write_text("obj-m += hello.o\n")
                (solution / "main.c").write_text("int value;\n")

            provider = _exercise("provider", "starter")
            provider = Exercise(**{
                **provider.__dict__,
                "resources": Resources("", "", "", "", "provider"),
            })
            consumer = _exercise(
                "consumer", "parent", parent="provider"
            )
            consumer = Exercise(**{
                **consumer.__dict__,
                "resources": Resources("", "", "", "", "consumer"),
            })

            catalog = self.catalog((provider, consumer))
            catalog.root = root
            with self.assertRaisesRegex(WorkspaceError, "byte-identical"):
                catalog.solution_tree(consumer)

    def test_imports_compose_for_every_workspace_baseline(self) -> None:
        for baseline in ("starter", "parent", "scaffold"):
            with self.subTest(baseline=baseline), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                donor = _exercise(
                    "donor",
                    "starter",
                    files=("shared.c",),
                )
                donor = Exercise(**{
                    **donor.__dict__,
                    "resources": Resources("", "", "", "", "donor-solution"),
                })
                base = _exercise(
                    "base",
                    "starter",
                    files=("base.c",),
                )
                base = Exercise(**{
                    **base.__dict__,
                    "resources": Resources("", "", "", "", "base-solution"),
                })
                local_name = "main.c" if baseline == "starter" else "base.c"
                consumer = _exercise(
                    "consumer",
                    baseline,
                    files=(local_name, "imported/shared.c"),
                    parent="" if baseline == "starter" else "base",
                )
                consumer = Exercise(**{
                    **consumer.__dict__,
                    "prerequisites": ("donor",),
                    "workspace": Workspace(**{
                        **consumer.workspace.__dict__,
                        "source": (
                            "consumer-starter"
                            if baseline in {"starter", "scaffold"}
                            else ""
                        ),
                        "imports": (
                            WorkspaceImport(
                                "donor",
                                (("shared.c", "imported/shared.c"),),
                            ),
                        ),
                    }),
                    "resources": Resources(
                        "", "", "", "", "consumer-solution"
                    ),
                })
                for name in (
                    "donor-solution",
                    "base-solution",
                    "consumer-starter",
                    "consumer-solution",
                ):
                    (root / name).mkdir()
                (root / "donor-solution/shared.c").write_text("shared\n")
                (root / "base-solution/base.c").write_text("base\n")
                if baseline == "starter":
                    (root / "consumer-starter/main.c").write_text("todo\n")
                    (root / "consumer-solution/main.c").write_text("done\n")
                elif baseline == "scaffold":
                    (root / "consumer-starter/base.c").write_text("todo\n")
                    (root / "consumer-solution/base.c").write_text("done\n")
                else:
                    (root / "consumer-solution/base.c").write_text("done\n")

                catalog = self.catalog((donor, base, consumer))
                catalog.root = root
                catalog.validate_workspace_imports(consumer)
                starter = catalog.starter_tree(consumer)
                solution = catalog.solution_tree(consumer)
                self.assertEqual(
                    starter["imported/shared.c"].data,
                    b"shared\n",
                )
                self.assertEqual(
                    solution["imported/shared.c"].data,
                    b"shared\n",
                )
                self.assertEqual(solution[local_name].data, b"done\n")

    def test_import_provider_must_be_a_prerequisite_with_declared_sources(self) -> None:
        provider = _exercise(
            "provider",
            "starter",
            files=("shared.c",),
        )
        consumer = _exercise(
            "consumer",
            "starter",
            files=("main.c", "copy.c"),
        )
        imported = WorkspaceImport("provider", (("shared.c", "copy.c"),))
        consumer = Exercise(**{
            **consumer.__dict__,
            "workspace": Workspace(**{
                **consumer.workspace.__dict__,
                "imports": (imported,),
            }),
        })
        catalog = self.catalog((provider, consumer))
        with self.assertRaisesRegex(CatalogError, "direct or transitive prerequisite"):
            catalog.validate_workspace_imports(consumer)

        consumer = Exercise(**{
            **consumer.__dict__,
            "prerequisites": ("provider",),
            "workspace": Workspace(**{
                **consumer.workspace.__dict__,
                "imports": (
                    WorkspaceImport("provider", (("missing.c", "copy.c"),)),
                ),
            }),
        })
        catalog = self.catalog((provider, consumer))
        with self.assertRaisesRegex(CatalogError, "source is not declared"):
            catalog.validate_workspace_imports(consumer)


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
