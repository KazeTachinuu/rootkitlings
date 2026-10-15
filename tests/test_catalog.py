from pathlib import Path
import json
import subprocess
import tempfile
import unittest

from engine.catalog import (
    Catalog, CatalogError, Exercise, Resources, Verification,
    VerificationCase, Workspace,
)


ROOT = Path(__file__).resolve().parent.parent


class GeneratedSerialTests(unittest.TestCase):
    def setUp(self) -> None:
        self.catalog = Catalog(ROOT)

    def test_course_does_not_store_aliases(self) -> None:
        import tomllib
        course = tomllib.loads((ROOT / "course.toml").read_text(encoding="utf-8"))
        self.assertNotIn("aliases", course)

    def test_serials_follow_route_and_kind(self) -> None:
        prefixes = {"lesson": "L", "milestone": "M", "final-project": "P"}
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
        self.assertEqual(
            list(self.catalog.required_route),
            [item.id for item in self.catalog.required()],
        )

    def test_selectors_accept_exact_aliases_and_unique_prefixes(self) -> None:
        self.assertEqual("module-build-lifecycle", self.catalog.resolve("l00").id)
        self.assertEqual(
            "module-build-lifecycle",
            self.catalog.resolve("module-build").id,
        )

    def test_unique_final_project_prefix_resolves_p00(self) -> None:
        self.assertEqual("complete-rootkit", self.catalog.resolve("P0").id)

    def test_catalog_query_and_export_resolve_default_module_artifact(self) -> None:
        query = subprocess.run(
            [str(ROOT / "scripts/catalog"), "query", "L00", "module_artifact"],
            cwd=ROOT,
            check=True,
            text=True,
            capture_output=True,
        )
        self.assertEqual(query.stdout, "rklings.ko\n")
        exported = subprocess.run(
            [str(ROOT / "scripts/catalog"), "export"],
            cwd=ROOT,
            check=True,
            text=True,
            capture_output=True,
        )
        data = json.loads(exported.stdout)
        self.assertEqual(data["default_module_artifact"], "rklings.ko")
        self.assertEqual(
            data["exercises"][0]["workspace"]["module_artifact"],
            "rklings.ko",
        )
        self.assertIsNone(
            data["exercises"][0]["workspace"]["module_artifact_override"]
        )
        self.assertEqual(
            data["exercises"][0]["verification"]["cases"],
            [{
                "id": "normal-lifecycle",
                "label": "Normal load and unload",
                "build_variables": {"RKLINGS_FAIL_POINT": "0"},
                "expect_build": "pass",
                "check": (
                    "curriculum/L00-module-build-lifecycle/"
                    "checks/module-lifecycle-check"
                ),
                "build_output_contains": "",
                "check_arguments": [],
            }],
        )


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
                "tools": [],
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

    def test_cases_are_required_and_nonempty(self) -> None:
        with self.assertRaisesRegex(CatalogError, "must be non-empty"):
            self.parse([])
        catalog = Catalog.__new__(Catalog)
        with self.assertRaisesRegex(CatalogError, r"missing=\['cases'\]"):
            catalog._verification(
                {"timeout_seconds": 30, "tools": []},
                Path("exercise.toml"),
            )

    def test_empty_case_fields_have_context_aware_defaults(self) -> None:
        verification = self.parse([
            {
                "id": "control",
                "label": "Control",
                "expect_build": "pass",
                "check": "check",
            },
            {
                "id": "rejected",
                "label": "Rejected build",
                "expect_build": "fail",
            },
        ])
        rejected = verification.cases[1]
        self.assertEqual(rejected.build_variables, ())
        self.assertEqual(rejected.check, "")
        self.assertEqual(rejected.build_output_contains, "")
        self.assertEqual(rejected.check_arguments, ())

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
                "id": "control", "label": "Control",
                "expect_build": "pass", "check": "check",
            },
            {
                "id": "failure", "label": "Failure",
                "expect_build": "fail",
            },
        ])
        self.assertEqual(failure.cases[1].build_output_contains, "")

    def test_cases_require_control_before_failures(self) -> None:
        failure = {
            "id": "failure", "label": "Failure", "expect_build": "fail",
        }
        control = {
            "id": "control", "label": "Control",
            "expect_build": "pass", "check": "check",
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
            item.verification.tools,
            (VerificationCase(
                "normal", "Normal", (), "pass", "missing/guest-check", ""
            ),),
        )
        explicit = Exercise(**{**item.__dict__, "verification": verification})
        with self.assertRaisesRegex(CatalogError, "missing verification case check"):
            catalog._validate_paths(explicit)


class RuntimeMetadataTests(unittest.TestCase):
    def test_optional_runtime_metadata_is_manifest_owned_and_queryable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            setup = root / "vm/runtime/example"
            setup.parent.mkdir(parents=True)
            setup.write_text("#!/bin/sh\n", encoding="utf-8")
            catalog = Catalog.__new__(Catalog)
            catalog.root = root
            runtime = catalog._runtime(
                {"setup": "vm/runtime/example"},
                Path("curriculum/L00-example/exercise.toml"),
            )
        from engine.catalog import Runtime
        self.assertEqual(
            runtime,
            Runtime("vm/runtime/example"),
        )

    def test_runtime_metadata_rejects_unknown_fields_and_unsafe_paths(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            catalog = Catalog.__new__(Catalog)
            catalog.root = Path(directory)
            for value in (
                {"setup": "../escape"},
                {"setup": "vm/runtime/example", "extra": True},
            ):
                with self.subTest(value=value):
                    with self.assertRaises(CatalogError):
                        catalog._runtime(value, Path("exercise.toml"))


def _exercise(
    stable_id: str,
    *,
    kind: str = "lesson",
    files: tuple[str, ...] = ("Kbuild", "main.c"),
    build: str = ".",
    module_artifact: str = "",
) -> Exercise:
    workspace = Workspace(
        mode="project",
        source="starter",
        files=files,
        editable=("main.c",),
        build=build,
        baseline="starter",
        module_artifact=module_artifact,
    )
    resources = Resources("README", "", "hint", "solution")
    verification = Verification(
        30,
        (),
        (VerificationCase("normal", "Normal", (), "pass", "check", ""),),
    )
    return Exercise(
        id=stable_id,
        path=Path(stable_id),
        title=stable_id,
        stage="stage",
        kind=kind,
        tradecraft=(),
        difficulty="standard",
        prerequisites=(),
        teaches=(f"concept-{stable_id}",),
        requires=(),
        workspace=workspace,
        resources=resources,
        verification=verification,
    )


class WorkspaceTests(unittest.TestCase):
    def catalog(self, route: tuple[Exercise, ...], extras=()) -> Catalog:
        catalog = Catalog.__new__(Catalog)
        catalog.required_route = tuple(item.id for item in route)
        catalog.exercises = list(route) + list(extras)
        catalog.by_id = {item.id: item for item in catalog.exercises}
        catalog.default_module_artifact = "hello.ko"
        return catalog

    def test_only_starter_baseline_is_accepted(self) -> None:
        catalog = Catalog.__new__(Catalog)
        catalog.root = ROOT
        workspace = {
            "mode": "project",
            "files": ["Kbuild", "main.c"],
            "editable": ["main.c"],
            "build": ".",
            "source": "starter",
        }
        for baseline in (
            "parent",
            "scaffold",
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

    def test_final_project_is_unique_and_last_when_present(self) -> None:
        lessons = tuple(
            _exercise(f"lesson-{index}") for index in range(3)
        )
        milestones = tuple(
            _exercise(f"milestone-{index}", kind="milestone")
            for index in range(6)
        )
        self.catalog((*lessons, *milestones))._validate_required_route()

        final_project = _exercise("final-project", kind="final-project")
        self.catalog(
            (*lessons, *milestones, final_project)
        )._validate_required_route()

        trailing = _exercise("trailing")
        with self.assertRaisesRegex(CatalogError, "end the required route"):
            self.catalog(
                (*lessons, final_project, trailing)
            )._validate_required_route()

        other = _exercise("other-project", kind="final-project")
        with self.assertRaisesRegex(CatalogError, "final project must be unique"):
            self.catalog(
                (*lessons, final_project, other)
            )._validate_required_route()

    def test_direct_prerequisites_reject_duplicates_and_transitive_edges(self) -> None:
        first = _exercise("first")
        second = Exercise(**{
            **_exercise("second").__dict__,
            "prerequisites": ("first",),
        })
        duplicate = Exercise(**{
            **_exercise("duplicate").__dict__,
            "prerequisites": ("second", "second"),
        })
        catalog = self.catalog((first, second, duplicate))
        with self.assertRaisesRegex(CatalogError, "duplicate prerequisites"):
            catalog._validate_direct_prerequisites()

        redundant = Exercise(**{
            **_exercise("redundant").__dict__,
            "prerequisites": ("first", "second"),
        })
        catalog = self.catalog((first, second, redundant))
        with self.assertRaisesRegex(
            CatalogError,
            "prerequisite 'first' is already implied by 'second'",
        ):
            catalog._validate_direct_prerequisites()

    def test_module_artifact_uses_default_and_override(self) -> None:
        defaulted = _exercise("defaulted")
        overridden = _exercise("overridden", module_artifact="rklings.ko")
        catalog = self.catalog((overridden,), (defaulted,))
        self.assertEqual(catalog.module_artifact(defaulted), "hello.ko")
        self.assertEqual(catalog.module_artifact(overridden), "rklings.ko")


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
            _module_artifact("rklings_test-1.ko", Path("test")),
            "rklings_test-1.ko",
        )


if __name__ == "__main__":
    unittest.main()
