"""Dependency-free loader and validator for the Rklings catalog."""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path, PurePosixPath
import re
import tomllib

from .solutions import (
    SolutionTree,
    full_solution_tree,
    materialize_solution_tree,
)
from .workspace import WorkspaceError


ID_RE = re.compile(r"[a-z0-9][a-z0-9-]*\Z")
SERIAL_RE = re.compile(r"(?:L|M|P)[0-9]{2}\Z")
TOKEN_RE = re.compile(r"[a-z0-9][a-z0-9-]*\Z")
MODULE_ARTIFACT_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]*\.ko\Z")
MAKE_VARIABLE_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
DIFFICULTIES = {"intro", "standard", "advanced", "challenge"}
KINDS = {"lesson", "milestone", "final-project"}
WORKSPACE_MODES = {"single-file", "project"}
BASELINES = {"starter"}
TOP_FIELDS = {
    "schema_version", "id", "title", "stage", "kind",
    "tradecraft", "difficulty", "prerequisites", "teaches", "requires",
    "workspace", "resources", "verification",
}
RUNTIME_FIELDS = {"setup"}
RESOURCE_FIELDS = {"readme", "host_check", "hint", "solution"}
VERIFICATION_FIELDS = {"timeout_seconds", "tools", "cases"}
VERIFICATION_CASE_REQUIRED_FIELDS = {"id", "label", "expect_build"}
VERIFICATION_CASE_OPTIONAL_FIELDS = {
    "build_variables", "check", "build_output_contains", "check_arguments",
    "network",
}
QUERY_FIELDS = {
    "id", "path", "serial", "sequence", "title", "stage", "kind",
    "tradecraft", "difficulty", "prerequisites", "teaches", "requires",
    "workspace_mode", "workspace_source", "workspace_files", "workspace_editable",
    "workspace_build", "workspace_baseline", "workspace_module_artifact",
    "readme", "host_check", "hint", "solution", "verification_timeout",
    "verification_tools", "runtime_setup",
}


class CatalogError(ValueError):
    pass


@dataclass(frozen=True)
class Assumption:
    concept: str
    competency: str


@dataclass(frozen=True)
class Section:
    id: str
    label: str


@dataclass(frozen=True)
class Concept:
    id: str
    label: str


@dataclass(frozen=True)
class Tool:
    id: str
    binary: str
    source: str


@dataclass(frozen=True)
class Workspace:
    mode: str
    source: str
    files: tuple[str, ...]
    editable: tuple[str, ...]
    build: str
    baseline: str
    module_artifact: str = ""


@dataclass(frozen=True)
class Resources:
    readme: str
    host_check: str
    hint: str
    solution: str


@dataclass(frozen=True)
class VerificationCase:
    id: str
    label: str
    build_variables: tuple[tuple[str, str], ...]
    expect_build: str
    check: str
    build_output_contains: str
    check_arguments: tuple[str, ...] = ()


@dataclass(frozen=True)
class Verification:
    timeout_seconds: int
    tools: tuple[str, ...]
    cases: tuple[VerificationCase, ...]


@dataclass(frozen=True)
class Runtime:
    """Optional target setup for operating a completed reference."""

    setup: str


@dataclass(frozen=True)
class Exercise:
    id: str
    path: Path
    title: str
    stage: str
    kind: str
    tradecraft: tuple[str, ...]
    difficulty: str
    prerequisites: tuple[str, ...]
    teaches: tuple[str, ...]
    requires: tuple[str, ...]
    workspace: Workspace
    resources: Resources
    verification: Verification
    runtime: Runtime | None = None
    serial: str = ""
    sequence: int = -1

    def value(self, field: str) -> object:
        if field not in QUERY_FIELDS:
            raise CatalogError(f"unknown field: {field}")
        values = {
            "id": self.id, "path": self.path.as_posix(), "serial": self.serial,
            "sequence": self.sequence,
            "title": self.title, "stage": self.stage,
            "kind": self.kind,
            "tradecraft": self.tradecraft, "difficulty": self.difficulty,
            "prerequisites": self.prerequisites, "teaches": self.teaches,
            "requires": self.requires, "workspace_mode": self.workspace.mode,
            "workspace_source": self.workspace.source, "workspace_files": self.workspace.files,
            "workspace_editable": self.workspace.editable,
            "workspace_build": self.workspace.build,
            "workspace_baseline": self.workspace.baseline,
            "workspace_module_artifact": self.workspace.module_artifact,
            "readme": self.resources.readme,
            "host_check": self.resources.host_check,
            "hint": self.resources.hint, "solution": self.resources.solution,
            "verification_timeout": self.verification.timeout_seconds,
            "verification_tools": self.verification.tools,
            "runtime_setup": self.runtime.setup if self.runtime else "",
        }
        return values[field]


class Catalog:
    def __init__(self, root: Path):
        self.root = root.resolve()
        (self.revision, self.default_module_artifact, self.exercise_roots,
         self.required_route, self.tradecraft_tags,
         self.tools, self.assumptions, self.sections,
         self.concepts) = self._read_course()
        self.concept_by_id = {item.id: item for item in self.concepts}
        raw = self._read_exercises()
        self.by_id = {item.id: item for item in raw}
        self.aliases = self._generate_aliases()
        reverse_alias = {exercise_id: alias for alias, exercise_id in self.aliases.items()}
        display_positions = {
            exercise_id: position
            for position, exercise_id in enumerate(self.required_route)
        }
        self.exercises = [
            Exercise(**{**item.__dict__, "serial": reverse_alias.get(item.id, ""),
                        "sequence": display_positions.get(item.id, -1)})
            for item in raw
        ]
        self.by_id = {item.id: item for item in self.exercises}
        self.validate()

    def _read_course(self):
        file = self.root / "course.toml"
        try:
            data = tomllib.loads(file.read_text(encoding="utf-8"))
        except (OSError, tomllib.TOMLDecodeError) as exc:
            raise CatalogError(f"cannot parse course.toml: {exc}") from exc
        required = {
            "schema_version", "revision", "default_module_artifact",
            "exercise_roots", "required_route",
            "tradecraft_tags", "tools", "assumptions",
            "sections", "concepts",
        }
        _exact_fields(data, required, Path("course.toml"))
        if data["schema_version"] != 2 or isinstance(data["schema_version"], bool):
            raise CatalogError("course.toml: schema_version must be integer 2")
        revision = _string(data, "revision", Path("course.toml"))
        default_module_artifact = _module_artifact(
            data["default_module_artifact"], Path("course.toml")
        )
        roots = tuple(_path_list(data, "exercise_roots", Path("course.toml"), self.root))
        for root in roots:
            if not (self.root / root).is_dir():
                raise CatalogError(f"course.toml: exercise root must be a directory: {root}")
        route = tuple(_id_list(data, "required_route", Path("course.toml")))
        assumptions = tuple(_rows(data, "assumptions", {"concept", "competency"}, Assumption, "concept"))
        sections = tuple(_rows(data, "sections", {"id", "label"}, Section, "id"))
        concepts = tuple(_rows(data, "concepts", {"id", "label"}, Concept, "id"))
        tradecraft_tags = tuple(_rows(data, "tradecraft_tags", {"id", "label"}, Concept, "id"))
        tools = tuple(_tool_rows(data, self.root))
        return (revision, default_module_artifact, roots, route, tradecraft_tags,
                tools, assumptions, sections, concepts)

    def _generate_aliases(self) -> dict[str, str]:
        prefixes = {"lesson": "L", "milestone": "M", "final-project": "P"}
        counters = {kind: 0 for kind in prefixes}
        aliases = {}
        for exercise_id in self.required_route:
            item = self.by_id.get(exercise_id)
            if item is None:
                raise CatalogError(f"required_route contains unknown ID: {exercise_id}")
            if item.kind not in prefixes:
                raise CatalogError(
                    f"{item.path}: required exercise has unsupported kind {item.kind!r}"
                )
            serial = f"{prefixes[item.kind]}{counters[item.kind]:02d}"
            counters[item.kind] += 1
            if not SERIAL_RE.fullmatch(serial):
                raise CatalogError(f"generated serial is out of range: {serial}")
            aliases[serial] = exercise_id
        return aliases

    def _read_exercises(self) -> list[Exercise]:
        files = sorted(
            file
            for root in self.exercise_roots
            for file in (self.root / root).glob("**/exercise.toml")
        )
        items = []
        for file in files:
            relative = file.relative_to(self.root)
            try:
                data = tomllib.loads(file.read_text(encoding="utf-8"))
            except (OSError, tomllib.TOMLDecodeError) as exc:
                raise CatalogError(f"cannot parse {relative}: {exc}") from exc
            _exact_fields(
                data,
                TOP_FIELDS | ({"runtime"} if "runtime" in data else set()),
                relative,
            )
            if data["schema_version"] != 2 or isinstance(data["schema_version"], bool):
                raise CatalogError(f"{relative}: schema_version must be integer 2")
            exercise_id = _identifier(data["id"], relative)
            path = file.parent.relative_to(self.root)
            workspace = self._workspace(data["workspace"], relative)
            resources = self._resources(data["resources"], relative)
            verification = self._verification(data["verification"], relative)
            runtime = (
                self._runtime(data["runtime"], relative)
                if "runtime" in data else None
            )
            items.append(Exercise(
                id=exercise_id, path=path, title=_string(data, "title", relative),
                stage=_token(data, "stage", relative), kind=_token(data, "kind", relative),
                tradecraft=tuple(_token_list(data, "tradecraft", relative)),
                difficulty=_token(data, "difficulty", relative),
                prerequisites=tuple(_id_list(data, "prerequisites", relative)),
                teaches=tuple(_token_list(data, "teaches", relative)),
                requires=tuple(_token_list(data, "requires", relative)),
                workspace=workspace, resources=resources,
                verification=verification, runtime=runtime,
            ))
        return items

    def _runtime(self, value, path):
        runtime_path = path / "runtime"
        if not isinstance(value, dict):
            raise CatalogError(f"{runtime_path}: runtime must be a table")
        _exact_fields(value, RUNTIME_FIELDS, runtime_path)
        return Runtime(
            setup=_safe_path(value["setup"], runtime_path, self.root),
        )

    def _workspace(self, value, path):
        if not isinstance(value, dict):
            raise CatalogError(f"{path}: workspace must be a table")
        baseline = _token(value, "baseline", path)
        if baseline not in BASELINES:
            raise CatalogError(f"{path}: invalid workspace baseline {baseline!r}")
        expected = {"mode", "files", "editable", "build", "baseline", "source"}
        if "module_artifact" in value:
            expected.add("module_artifact")
        _exact_fields(value, expected, path / "workspace")
        source = value.get("source", "")
        if not source:
            raise CatalogError(f"{path}: starter baseline requires source")
        module_artifact = value.get("module_artifact", "")
        return Workspace(
            mode=_token(value, "mode", path),
            source=_safe_path(source, path, self.root),
            files=tuple(_relative_list(value, "files", path)),
            editable=tuple(_relative_list(value, "editable", path)),
            build=_workspace_build(value["build"], path),
            baseline=baseline,
            module_artifact=(
                _module_artifact(module_artifact, path / "workspace")
                if module_artifact else ""
            ),
        )

    def _resources(self, value, path):
        if not isinstance(value, dict):
            raise CatalogError(f"{path}: resources must be a table")
        _exact_fields(value, RESOURCE_FIELDS, path / "resources")
        return Resources(**{key: _optional_path(value[key], path, self.root) for key in RESOURCE_FIELDS})

    def _verification(self, value, path):
        if not isinstance(value, dict):
            raise CatalogError(f"{path}: verification must be a table")
        _exact_fields(value, VERIFICATION_FIELDS, path / "verification")
        timeout = value["timeout_seconds"]
        if isinstance(timeout, bool) or not isinstance(timeout, int) or timeout <= 0:
            raise CatalogError(f"{path}: verification timeout must be a positive integer")
        raw_cases = value["cases"]
        if not isinstance(raw_cases, list):
            raise CatalogError(f"{path}: verification cases must be an array of tables")
        cases = tuple(
            self._verification_case(case, path, index)
            for index, case in enumerate(raw_cases)
        )
        if not cases:
            raise CatalogError(f"{path}: verification cases must be non-empty")
        case_ids = [case.id for case in cases]
        case_labels = [case.label.casefold() for case in cases]
        if len(case_ids) != len(set(case_ids)):
            raise CatalogError(f"{path}: duplicate verification case ids")
        if len(case_labels) != len(set(case_labels)):
            raise CatalogError(f"{path}: duplicate verification case labels")
        if not any(case.expect_build == "pass" for case in cases):
            raise CatalogError(
                f"{path}: verification cases require a successful control"
            )
        saw_failure = False
        for case in cases:
            if case.expect_build == "fail":
                saw_failure = True
            elif saw_failure:
                raise CatalogError(
                    f"{path}: successful verification cases must precede failures"
                )
        return Verification(timeout, tuple(_token_list(value, "tools", path)), cases)

    def _verification_case(self, value, path, index):
        case_path = path / "verification" / "cases" / str(index)
        if not isinstance(value, dict):
            raise CatalogError(f"{case_path}: verification case must be a table")
        fields = set(value)
        missing = VERIFICATION_CASE_REQUIRED_FIELDS - fields
        unknown = fields - (
            VERIFICATION_CASE_REQUIRED_FIELDS | VERIFICATION_CASE_OPTIONAL_FIELDS
        )
        if missing or unknown:
            raise CatalogError(
                f"{case_path}: fields mismatch "
                f"(missing={sorted(missing)}, unknown={sorted(unknown)})"
            )
        case_id = _identifier(value["id"], case_path)
        label = _bounded_printable(value["label"], "label", case_path, empty=False)
        if label != label.strip():
            raise CatalogError(f"{case_path}: label must not have surrounding whitespace")
        variables = value.get("build_variables", {})
        if not isinstance(variables, dict):
            raise CatalogError(f"{case_path}: build_variables must be a table")
        normalized = []
        for name, variable_value in variables.items():
            if not isinstance(name, str) or not MAKE_VARIABLE_RE.fullmatch(name):
                raise CatalogError(f"{case_path}: unsafe make variable name {name!r}")
            normalized.append((
                name,
                _bounded_printable(
                    variable_value, f"build_variables.{name}", case_path, empty=True
                ),
            ))
        expect_build = value["expect_build"]
        if not isinstance(expect_build, str) or expect_build not in {"pass", "fail"}:
            raise CatalogError(f"{case_path}: expect_build must be 'pass' or 'fail'")
        check = _optional_path(value.get("check", ""), case_path, self.root)
        output = _bounded_printable(
            value.get("build_output_contains", ""),
            "build_output_contains",
            case_path,
            empty=True,
        )
        if expect_build == "pass":
            if not check:
                raise CatalogError(f"{case_path}: successful case requires a guest check")
            if output:
                raise CatalogError(
                    f"{case_path}: successful case forbids build_output_contains"
                )
        elif check:
            raise CatalogError(
                f"{case_path}: failed case forbids a guest check"
            )
        raw_arguments = value.get("check_arguments", [])
        if not isinstance(raw_arguments, list) or len(raw_arguments) > 16:
            raise CatalogError(
                f"{case_path}: check_arguments must be an array of at most 16 strings"
            )
        check_arguments = tuple(
            _bounded_printable(
                argument,
                f"check_arguments.{argument_index}",
                case_path,
                empty=True,
            )
            for argument_index, argument in enumerate(raw_arguments)
        )
        if expect_build == "fail" and check_arguments:
            raise CatalogError(
                f"{case_path}: failed case forbids check_arguments"
            )
        return VerificationCase(
            case_id,
            label,
            tuple(sorted(normalized)),
            expect_build,
            check,
            output,
            check_arguments,
        )

    def validate(self) -> None:
        _unique(self.exercises, "id")
        _unique(self.exercises, "path")
        if len(self.required_route) != len(set(self.required_route)):
            raise CatalogError("course.toml: required_route contains duplicates")
        actual = set(self.by_id)
        route = set(self.required_route)
        if route != actual:
            raise CatalogError(
                "required_route mismatch "
                f"(missing={sorted(actual-route)}, unknown={sorted(route-actual)})"
            )
        if set(self.aliases.values()) != route:
            raise CatalogError("generated serial aliases must cover the required route")
        self._validate_required_route()
        section_ids = {item.id for item in self.sections}
        if len(section_ids) != len(self.sections):
            raise CatalogError("course.toml: duplicate section ids")
        concept_ids = {item.id for item in self.concepts}
        if len(concept_ids) != len(self.concepts):
            raise CatalogError("course.toml: duplicate concept ids")
        baseline = {item.concept for item in self.assumptions}
        tradecraft_ids = {item.id for item in self.tradecraft_tags}
        if len(tradecraft_ids) != len(self.tradecraft_tags):
            raise CatalogError("course.toml: duplicate tradecraft tag ids")
        tool_ids = {item.id for item in self.tools}
        _unique_registry(self.assumptions, "concept", "competency", "assumptions")
        _unique_registry(self.sections, "id", "label", "sections")
        _unique_registry(self.concepts, "id", "label", "concepts")
        _unique_registry(self.tradecraft_tags, "id", "label", "tradecraft tags")
        _unique_registry(self.tools, "id", "binary", "tools")
        teachers = {}
        for item in self.exercises:
            self._validate_bundle_path(item)
            if item.stage not in section_ids or item.kind not in KINDS:
                raise CatalogError(f"{item.path}: invalid stage or kind")
            if item.serial.startswith("L") and item.kind != "lesson":
                raise CatalogError(f"{item.path}: L alias requires kind='lesson'")
            if item.serial.startswith("P") and item.kind != "final-project":
                raise CatalogError(
                    f"{item.path}: C alias requires kind='final-project'"
                )
            if item.serial.startswith("M") and item.kind != "milestone":
                raise CatalogError(f"{item.path}: M alias requires kind='milestone'")
            if set(item.tradecraft) - tradecraft_ids or item.difficulty not in DIFFICULTIES:
                raise CatalogError(f"{item.path}: invalid tradecraft tag or difficulty")
            if item.workspace.mode not in WORKSPACE_MODES or item.workspace.baseline not in BASELINES:
                raise CatalogError(f"{item.path}: invalid workspace mode or baseline")
            if set(item.verification.tools) - tool_ids:
                raise CatalogError(f"{item.path}: unknown verification tools")
            if not item.teaches or len(set(item.teaches)) != len(item.teaches):
                raise CatalogError(f"{item.path}: teaches must be non-empty and unique")
            if set(item.teaches) & set(item.requires):
                raise CatalogError(f"{item.path}: taught and required concepts overlap")
            for concept in item.teaches:
                if concept in teachers:
                    raise CatalogError(f"concept {concept!r} has multiple teachers")
                teachers[concept] = item.id
            for dependency in item.prerequisites:
                if dependency not in actual:
                    raise CatalogError(f"{item.path}: unknown prerequisite {dependency!r}")
            self._validate_paths(item)
        self._validate_dag()
        self._validate_direct_prerequisites()
        solution_cache: dict[str, SolutionTree] = {}
        for item in self.exercises:
            try:
                self.solution_tree(item, solution_cache)
            except WorkspaceError as exc:
                raise CatalogError(f"{item.path}: invalid solution: {exc}") from exc
        for item in self.exercises:
            available = baseline | self.prerequisite_concepts(item)
            missing = set(item.requires) - available
            if missing:
                raise CatalogError(f"{item.path}: unavailable required concepts: {sorted(missing)}")
        used = baseline | set(teachers) | {value for item in self.exercises for value in item.requires}
        if used != concept_ids:
            raise CatalogError(f"course.toml: concept coverage mismatch (missing={sorted(used-concept_ids)}, unknown={sorted(concept_ids-used)})")
        used_tradecraft = {tag for item in self.exercises for tag in item.tradecraft}
        if used_tradecraft != tradecraft_ids:
            raise CatalogError(f"course.toml: unused tradecraft tags: {sorted(tradecraft_ids-used_tradecraft)}")
        used_tools = {value for item in self.exercises for value in item.verification.tools}
        if used_tools != tool_ids:
            raise CatalogError(f"course.toml: tool registry usage mismatch: {sorted(tool_ids-used_tools)}")

    def _validate_bundle_path(self, item: Exercise) -> None:
        """Keep bundle names tied to route-derived serials."""
        expected_name = f"{item.serial}-{item.id}"
        roots = tuple(Path(root) for root in self.exercise_roots)
        if item.path.parent not in roots or item.path.name != expected_name:
            expected = roots[0] / expected_name
            raise CatalogError(
                f"{item.path}: required bundle path must be {expected}"
            )

    def _validate_required_route(self) -> None:
        """Validate generic structural invariants of the required route."""
        positions = {
            exercise_id: position
            for position, exercise_id in enumerate(self.required_route)
        }
        final_projects = [
            item for item in self.exercises if item.kind == "final-project"
        ]
        if len(final_projects) > 1:
            raise CatalogError("course.toml: final project must be unique")
        if final_projects and (
            not self.required_route
            or final_projects[0].id != self.required_route[-1]
        ):
            raise CatalogError(
                "course.toml: final project must end the required route"
            )

        for item in self.exercises:
            if item.id in positions:
                for dependency in item.prerequisites:
                    if dependency in self.by_id and dependency not in positions:
                        raise CatalogError(
                            f"{item.path}: required exercise cannot depend on "
                            f"non-required exercise {dependency!r}"
                        )
                    if (
                        dependency in positions
                        and positions[dependency] >= positions[item.id]
                    ):
                        raise CatalogError(
                            f"{item.path}: prerequisite {dependency!r} is not "
                            "earlier in required_route"
                        )

    def _validate_paths(self, item):
        required_files = [item.resources.readme, item.resources.hint]
        optional_files = [item.resources.host_check]
        if item.runtime is not None:
            required_files.append(item.runtime.setup)
        for value in required_files:
            if not value or not (self.root / value).is_file():
                raise CatalogError(f"{item.path}: missing resource file: {value!r}")
        for value in optional_files:
            if value and not (self.root / value).is_file():
                raise CatalogError(f"{item.path}: missing optional resource file: {value}")
        for case in item.verification.cases:
            if case.check and not (self.root / case.check).is_file():
                raise CatalogError(
                    f"{item.path}: missing verification case check: {case.check}"
                )
        if not item.resources.solution or not (self.root / item.resources.solution).is_dir():
            raise CatalogError(f"{item.path}: solution must be an existing directory")
        if not (self.root / item.workspace.source).is_dir():
            raise CatalogError(f"{item.path}: workspace source must be an existing directory")
        source = self.root / item.workspace.source
        for relative in item.workspace.files:
            if not (source / relative).is_file():
                raise CatalogError(f"{item.path}: workspace file missing from source: {relative}")
        build = source if item.workspace.build == "." else source / item.workspace.build
        if not build.is_dir():
            raise CatalogError(f"{item.path}: workspace build directory is missing")
        if not set(item.workspace.editable) <= set(item.workspace.files):
            raise CatalogError(f"{item.path}: editable paths must be listed in workspace files")
    def _validate_dag(self):
        visiting, visited = set(), set()
        def visit(exercise_id):
            if exercise_id in visiting:
                raise CatalogError(f"dependency cycle at {exercise_id}")
            if exercise_id in visited:
                return
            visiting.add(exercise_id)
            for parent in self.by_id[exercise_id].prerequisites:
                visit(parent)
            visiting.remove(exercise_id); visited.add(exercise_id)
        for exercise_id in self.by_id:
            visit(exercise_id)

    def _validate_direct_prerequisites(self) -> None:
        """Reject duplicate or transitively redundant dependency edges."""
        for item in self.exercises:
            direct = item.prerequisites
            if len(direct) != len(set(direct)):
                raise CatalogError(f"{item.path}: duplicate prerequisites")
            for dependency in direct:
                for other in direct:
                    if dependency == other:
                        continue
                    if dependency in self.prerequisite_ids(self.by_id[other]):
                        raise CatalogError(
                            f"{item.path}: prerequisite {dependency!r} is already "
                            f"implied by {other!r}"
                        )

    def resolve(self, selector: str) -> Exercise:
        normalized = selector.upper()
        exercise_id = self.aliases.get(normalized, selector)
        if exercise_id in self.by_id:
            return self.by_id[exercise_id]
        matches = {
            stable_id
            for alias, stable_id in self.aliases.items()
            if alias.startswith(normalized)
        }
        matches.update(
            stable_id for stable_id in self.by_id if stable_id.startswith(selector)
        )
        if len(matches) == 1:
            return self.by_id[matches.pop()]
        if matches:
            labels = sorted(
                self.by_id[stable_id].serial or stable_id for stable_id in matches
            )
            raise CatalogError(
                f"ambiguous exercise {selector!r}; matches: {', '.join(labels)}"
            )
        raise CatalogError(f"unknown exercise: {selector}")

    def required(self) -> tuple[Exercise, ...]:
        return tuple(self.by_id[value] for value in self.required_route)

    def module_artifact(self, item: Exercise) -> str:
        return item.workspace.module_artifact or self.default_module_artifact

    def solution_tree(
        self,
        item: Exercise,
        cache: dict[str, SolutionTree] | None = None,
    ) -> SolutionTree:
        """Load the standalone reference solution for an exercise."""
        cache = {} if cache is None else cache
        if item.id in cache:
            return dict(cache[item.id])
        result = full_solution_tree(
            self.root / item.resources.solution, item.workspace.files
        )
        cache[item.id] = result
        return dict(result)

    def starter_tree(self, item: Exercise) -> SolutionTree:
        """Load the standalone starter for an exercise."""
        return full_solution_tree(
            self.root / item.workspace.source, item.workspace.files
        )

    def materialize_starter(
        self,
        item: Exercise,
        destination_root: Path,
        stable_id: str | None = None,
    ) -> Path:
        return materialize_solution_tree(
            self.starter_tree(item),
            destination_root,
            stable_id or item.id,
        )

    def materialize_solution(
        self,
        item: Exercise,
        destination_root: Path,
        stable_id: str | None = None,
    ) -> Path:
        """Atomically materialize an exercise's fully composed solution tree."""
        return materialize_solution_tree(
            self.solution_tree(item),
            destination_root,
            stable_id or item.id,
        )

    def completion_dependencies(self, item: Exercise) -> tuple[str, ...]:
        return tuple(item.prerequisites)

    def prerequisite_exercises(self, item: Exercise) -> tuple[Exercise, ...]:
        ids = self.prerequisite_ids(item)
        return tuple(sorted((self.by_id[value] for value in ids), key=lambda value: value.sequence))

    def prerequisite_ids(self, item: Exercise) -> set[str]:
        seen = set()
        def visit(exercise_id):
            if exercise_id in seen: return
            seen.add(exercise_id)
            for parent in self.by_id[exercise_id].prerequisites: visit(parent)
        for value in item.prerequisites: visit(value)
        return seen

    def prerequisite_concepts(self, item: Exercise) -> set[str]:
        return {concept for parent in self.prerequisite_exercises(item) for concept in parent.teaches}

    def all_concepts(self, item: Exercise) -> set[str]:
        return {value.concept for value in self.assumptions} | self.prerequisite_concepts(item)

    def assumption(self, concept_id: str) -> Assumption | None:
        return next((item for item in self.assumptions if item.concept == concept_id), None)

    def concept_label(self, concept_id: str) -> str:
        try: return self.concept_by_id[concept_id].label
        except KeyError as exc: raise CatalogError(f"unknown concept: {concept_id}") from exc

    def section(self, item: Exercise) -> str:
        return next(value.label for value in self.sections if value.id == item.stage)

def _exact_fields(data, expected, path):
    unknown, missing = set(data) - expected, expected - set(data)
    if unknown or missing:
        raise CatalogError(f"{path}: fields mismatch (missing={sorted(missing)}, unknown={sorted(unknown)})")


def _rows(data, key, fields, cls, id_field):
    value = data[key]
    if not isinstance(value, list):
        raise CatalogError(f"course.toml: {key} must be an array of tables")
    if not value:
        raise CatalogError(f"course.toml: {key} must be non-empty")
    result = []
    for index, row in enumerate(value):
        _exact_fields(row, fields, Path(f"course.toml/{key}/{index}"))
        ident, label = row[id_field], row[next(v for v in fields if v != id_field)]
        if not isinstance(ident, str) or not TOKEN_RE.fullmatch(ident) or not isinstance(label, str) or not label.strip():
            raise CatalogError(f"course.toml: invalid {key} row {index}")
        result.append(cls(ident, label.strip()))
    return result


def _tool_rows(data, root):
    value = data["tools"]
    if not isinstance(value, list) or not value:
        raise CatalogError("course.toml: tools must be non-empty")
    result = []
    for index, row in enumerate(value):
        path = Path(f"course.toml/tools/{index}")
        _exact_fields(row, {"id", "binary", "source"}, path)
        tool_id = _identifier(row["id"], path)
        binary = _safe_path(row["binary"], path, root)
        source = _safe_path(row["source"], path, root)
        if not (root / source).is_file():
            raise CatalogError(f"{path}: tool source must exist: {source}")
        result.append(Tool(tool_id, binary, source))
    return result


def _unique_registry(items, id_field, label_field, name):
    ids = [getattr(item, id_field) for item in items]
    labels = [getattr(item, label_field) for item in items]
    if len(ids) != len(set(ids)):
        raise CatalogError(f"course.toml: duplicate {name} ids")
    normalized = [value.casefold() for value in labels]
    if len(normalized) != len(set(normalized)):
        raise CatalogError(f"course.toml: duplicate {name} labels")


def _string(data, key, path):
    value = data[key]
    if not isinstance(value, str) or not value.strip(): raise CatalogError(f"{path}: {key} must be non-empty")
    return value.strip()


def _bounded_printable(value, field, path, *, empty):
    if (
        not isinstance(value, str)
        or (not empty and not value)
        or len(value) > 256
        or any(not character.isprintable() for character in value)
    ):
        qualifier = "printable string of at most 256 characters"
        if not empty:
            qualifier = f"non-empty {qualifier}"
        raise CatalogError(f"{path}: {field} must be a {qualifier}")
    return value


def _token(data, key, path):
    value = _string(data, key, path)
    if not TOKEN_RE.fullmatch(value): raise CatalogError(f"{path}: invalid {key} token {value!r}")
    return value


def _identifier(value, path):
    if not isinstance(value, str) or not ID_RE.fullmatch(value): raise CatalogError(f"{path}: invalid exercise id {value!r}")
    return value


def _list(data, key, path):
    value = data[key]
    if not isinstance(value, list): raise CatalogError(f"{path}: {key} must be an array")
    return value


def _token_list(data, key, path):
    values = _list(data, key, path)
    if any(not isinstance(v, str) or not TOKEN_RE.fullmatch(v) for v in values):
        raise CatalogError(f"{path}: {key} must contain slug tokens")
    if len(values) != len(set(values)): raise CatalogError(f"{path}: duplicate values in {key}")
    return values


def _id_list(data, key, path):
    values = _list(data, key, path)
    for value in values: _identifier(value, path)
    return values


def _safe_path(value, path, root):
    if not isinstance(value, str): raise CatalogError(f"{path}: path must be a string")
    candidate = PurePosixPath(value)
    if candidate.is_absolute() or not candidate.parts or any(part in {"", ".", ".."} for part in candidate.parts):
        raise CatalogError(f"{path}: unsafe path {value!r}")
    resolved = (root / candidate).resolve()
    if resolved != root and root not in resolved.parents: raise CatalogError(f"{path}: path escapes repository: {value}")
    return candidate.as_posix()


def _optional_path(value, path, root):
    return "" if value == "" else _safe_path(value, path, root)


def _path_list(data, key, path, root):
    values = _list(data, key, path)
    if not values: raise CatalogError(f"{path}: {key} must not be empty")
    result = [_safe_path(value, path, root) for value in values]
    if len(result) != len(set(result)): raise CatalogError(f"{path}: duplicate paths in {key}")
    return result


def _relative_list(data, key, path):
    values = _list(data, key, path)
    if not values:
        raise CatalogError(f"{path}: {key} must not be empty")
    result = []
    for value in values:
        if not isinstance(value, str):
            raise CatalogError(f"{path}: {key} must contain strings")
        result.append(_relative_path(value, path))
    if len(result) != len(set(result)):
        raise CatalogError(f"{path}: duplicate paths in {key}")
    return result


def _relative_path(value, path):
    if not isinstance(value, str):
        raise CatalogError(f"{path}: workspace-relative path must be a string")
    candidate = PurePosixPath(value)
    if (
        candidate.is_absolute()
        or not candidate.parts
        or any(part in {"", ".", ".."} for part in candidate.parts)
    ):
        raise CatalogError(f"{path}: unsafe workspace-relative path {value!r}")
    return candidate.as_posix()


def _workspace_build(value, path):
    if value == ".":
        return value
    if not isinstance(value, str):
        raise CatalogError(f"{path}: workspace build must be a string")
    candidate = PurePosixPath(value)
    if candidate.is_absolute() or any(part in {"", ".", ".."} for part in candidate.parts):
        raise CatalogError(f"{path}: unsafe workspace build path {value!r}")
    return candidate.as_posix()


def _module_artifact(value, path):
    if not isinstance(value, str) or not MODULE_ARTIFACT_RE.fullmatch(value):
        raise CatalogError(
            f"{path}: module artifact must be a portable .ko basename"
        )
    if value.startswith("-") or ".." in value:
        raise CatalogError(
            f"{path}: module artifact must be a portable .ko basename"
        )
    return value


def _unique(items, field):
    values = [getattr(item, field) for item in items]
    if len(values) != len(set(values)): raise CatalogError(f"duplicate catalog {field}")
