"""Dependency-free loader and validator for the rootkitlings catalog v2."""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path, PurePosixPath
import re
import tomllib

try:
    from .solutions import (
        SolutionTree,
        full_solution_tree,
        materialize_solution_tree,
        overlay_solution_tree,
    )
    from .workspace import WorkspaceError
except ImportError:  # scripts add lib/ directly to sys.path
    from solutions import (
        SolutionTree,
        full_solution_tree,
        materialize_solution_tree,
        overlay_solution_tree,
    )
    from workspace import WorkspaceError


ID_RE = re.compile(r"[a-z0-9][a-z0-9-]*\Z")
SERIAL_RE = re.compile(r"(?:L|M|C)[0-9]{2}\Z")
TRACK_SERIAL_RE = re.compile(r"E[0-9]{2}\Z")
TOKEN_RE = re.compile(r"[a-z0-9][a-z0-9-]*\Z")
MODULE_ARTIFACT_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]*\.ko\Z")
MAKE_VARIABLE_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
DIFFICULTIES = {"intro", "standard", "advanced", "challenge"}
KINDS = {"lesson", "milestone", "capstone"}
STATUSES = {"required", "elective", "retired"}
WORKSPACE_MODES = {"cumulative", "single-file", "project"}
BASELINES = {
    "template",
    "completion",
    "cumulative-root",
    "previous-required",
    "previous-milestone",
}
TOP_FIELDS = {
    "schema_version", "id", "title", "stage", "kind", "status",
    "tradecraft", "difficulty", "prerequisites", "teaches", "requires",
    "workspace", "resources", "verification",
}
RESOURCE_FIELDS = {"readme", "check", "host_check", "hint", "solution"}
VERIFICATION_FIELDS = {"timeout_seconds", "capabilities", "tools", "cleanup_required"}
VERIFICATION_CASE_FIELDS = {
    "id", "label", "build_variables", "expect_build", "check",
    "build_output_contains",
}
QUERY_FIELDS = {
    "id", "path", "serial", "sequence", "title", "stage", "kind",
    "route_position", "track_id", "track_position",
    "status", "tradecraft", "difficulty", "prerequisites", "teaches", "requires",
    "workspace_mode", "workspace_source", "workspace_files", "workspace_editable",
    "workspace_build", "workspace_baseline", "workspace_parent",
    "workspace_additions", "workspace_module_artifact",
    "readme", "check",
    "host_check", "hint", "solution", "verification_timeout", "verification_capabilities",
    "verification_tools", "verification_cleanup",
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
class ElectiveTrack:
    id: str
    label: str
    members: tuple[str, ...]
    serial: str = ""
    position: int = -1


@dataclass(frozen=True)
class Workspace:
    mode: str
    source: str
    files: tuple[str, ...]
    editable: tuple[str, ...]
    build: str
    baseline: str
    parent: str
    additions: str = ""
    module_artifact: str = ""


@dataclass(frozen=True)
class Resources:
    readme: str
    check: str
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


@dataclass(frozen=True)
class Verification:
    timeout_seconds: int
    capabilities: tuple[str, ...]
    tools: tuple[str, ...]
    cleanup_required: bool
    cases: tuple[VerificationCase, ...] = ()


@dataclass(frozen=True)
class Exercise:
    id: str
    path: Path
    title: str
    stage: str
    kind: str
    status: str
    tradecraft: tuple[str, ...]
    difficulty: str
    prerequisites: tuple[str, ...]
    teaches: tuple[str, ...]
    requires: tuple[str, ...]
    workspace: Workspace
    resources: Resources
    verification: Verification
    serial: str = ""
    sequence: int = -1
    route_position: int = -1
    track_id: str = ""
    track_position: int = -1

    def value(self, field: str) -> object:
        if field not in QUERY_FIELDS:
            raise CatalogError(f"unknown field: {field}")
        values = {
            "id": self.id, "path": self.path.as_posix(), "serial": self.serial,
            "sequence": self.sequence, "route_position": self.route_position,
            "track_id": self.track_id, "track_position": self.track_position,
            "title": self.title, "stage": self.stage,
            "kind": self.kind, "status": self.status,
            "tradecraft": self.tradecraft, "difficulty": self.difficulty,
            "prerequisites": self.prerequisites, "teaches": self.teaches,
            "requires": self.requires, "workspace_mode": self.workspace.mode,
            "workspace_source": self.workspace.source, "workspace_files": self.workspace.files,
            "workspace_editable": self.workspace.editable,
            "workspace_build": self.workspace.build,
            "workspace_baseline": self.workspace.baseline,
            "workspace_parent": self.workspace.parent,
            "workspace_additions": self.workspace.additions,
            "workspace_module_artifact": self.workspace.module_artifact,
            "readme": self.resources.readme,
            "check": self.resources.check, "host_check": self.resources.host_check,
            "hint": self.resources.hint, "solution": self.resources.solution,
            "verification_timeout": self.verification.timeout_seconds,
            "verification_capabilities": self.verification.capabilities,
            "verification_tools": self.verification.tools,
            "verification_cleanup": self.verification.cleanup_required,
        }
        return values[field]


class Catalog:
    def __init__(self, root: Path):
        self.root = root.resolve()
        (self.revision, self.default_module_artifact, self.exercise_roots,
         self.cumulative_root,
         self.required_route, raw_tracks, self.tradecraft_tags,
         self.capabilities, self.tools, self.assumptions, self.sections,
         self.concepts) = self._read_course()
        self.concept_by_id = {item.id: item for item in self.concepts}
        raw = self._read_exercises()
        self.by_id = {item.id: item for item in raw}
        self.aliases = self._generate_aliases()
        self.elective_tracks = self._generate_track_aliases(raw_tracks)
        self.track_aliases = {
            track.serial: track.id for track in self.elective_tracks
        }
        reverse_alias = {exercise_id: alias for alias, exercise_id in self.aliases.items()}
        route_positions = {
            exercise_id: position
            for position, exercise_id in enumerate(self.required_route)
        }
        track_membership = {
            exercise_id: (track.id, position)
            for track in self.elective_tracks
            for position, exercise_id in enumerate(track.members)
        }
        display_ids = list(self.required_route) + [
            exercise_id
            for track in self.elective_tracks
            for exercise_id in track.members
        ]
        display_positions = {
            exercise_id: position for position, exercise_id in enumerate(display_ids)
        }
        self.exercises = [
            Exercise(**{**item.__dict__, "serial": reverse_alias.get(item.id, ""),
                        "sequence": display_positions.get(item.id, -1),
                        "route_position": route_positions.get(item.id, -1),
                        "track_id": track_membership.get(item.id, ("", -1))[0],
                        "track_position": track_membership.get(item.id, ("", -1))[1]})
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
            "tradecraft_tags", "capabilities", "tools", "assumptions",
            "sections", "concepts",
        }
        expected = required
        if "elective_tracks" in data:
            expected.add("elective_tracks")
        if "cumulative_root" in data:
            expected.add("cumulative_root")
        _exact_fields(data, expected, Path("course.toml"))
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
        cumulative_root = data.get("cumulative_root", "")
        if cumulative_root:
            _identifier(cumulative_root, Path("course.toml"))
        elective_tracks = tuple(_elective_track_rows(data))
        assumptions = tuple(_rows(data, "assumptions", {"concept", "competency"}, Assumption, "concept"))
        sections = tuple(_rows(data, "sections", {"id", "label"}, Section, "id"))
        concepts = tuple(_rows(data, "concepts", {"id", "label"}, Concept, "id"))
        tradecraft_tags = tuple(_rows(data, "tradecraft_tags", {"id", "label"}, Concept, "id"))
        capabilities = tuple(_rows(
            data, "capabilities", {"id", "label"}, Concept, "id",
            allow_empty=True,
        ))
        tools = tuple(_tool_rows(data, self.root))
        return (revision, default_module_artifact, roots, cumulative_root,
                route, elective_tracks,
                tradecraft_tags,
                capabilities, tools, assumptions, sections, concepts)

    def _generate_aliases(self) -> dict[str, str]:
        prefixes = {"lesson": "L", "milestone": "M", "capstone": "C"}
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

    def _generate_track_aliases(
        self,
        tracks: tuple[ElectiveTrack, ...],
    ) -> tuple[ElectiveTrack, ...]:
        result = []
        for position, track in enumerate(tracks):
            serial = f"E{position + 1:02d}"
            if not TRACK_SERIAL_RE.fullmatch(serial):
                raise CatalogError(f"generated track serial is out of range: {serial}")
            result.append(ElectiveTrack(
                track.id,
                track.label,
                track.members,
                serial,
                position,
            ))
        return tuple(result)

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
            _exact_fields(data, TOP_FIELDS, relative)
            if data["schema_version"] != 2 or isinstance(data["schema_version"], bool):
                raise CatalogError(f"{relative}: schema_version must be integer 2")
            exercise_id = _identifier(data["id"], relative)
            path = file.parent.relative_to(self.root)
            workspace = self._workspace(data["workspace"], relative)
            resources = self._resources(data["resources"], relative)
            verification = self._verification(data["verification"], relative)
            items.append(Exercise(
                id=exercise_id, path=path, title=_string(data, "title", relative),
                stage=_token(data, "stage", relative), kind=_token(data, "kind", relative),
                status=_token(data, "status", relative),
                tradecraft=tuple(_token_list(data, "tradecraft", relative)),
                difficulty=_token(data, "difficulty", relative),
                prerequisites=tuple(_id_list(data, "prerequisites", relative)),
                teaches=tuple(_token_list(data, "teaches", relative)),
                requires=tuple(_token_list(data, "requires", relative)),
                workspace=workspace, resources=resources, verification=verification,
            ))
        return items

    def _workspace(self, value, path):
        if not isinstance(value, dict):
            raise CatalogError(f"{path}: workspace must be a table")
        baseline = _token(value, "baseline", path)
        common = {"mode", "files", "editable", "build", "baseline"}
        if "module_artifact" in value:
            common.add("module_artifact")
        if baseline == "template":
            expected = common | {"source"}
        elif baseline == "completion":
            expected = common | {"parent"} | ({"additions"} if "additions" in value else set())
        else:
            expected = common | ({"additions"} if "additions" in value else set())
        _exact_fields(value, expected, path / "workspace")
        source = value.get("source", "")
        parent = value.get("parent", "")
        additions = value.get("additions", "")
        module_artifact = value.get("module_artifact", "")
        if baseline == "template":
            if not source or parent:
                raise CatalogError(f"{path}: template baseline requires source and forbids parent")
        elif baseline == "completion":
            if source or not parent:
                raise CatalogError(f"{path}: completion baseline requires parent and forbids source")
            _identifier(parent, path)
        elif baseline in {
            "cumulative-root", "previous-required", "previous-milestone",
        }:
            if source or parent:
                raise CatalogError(
                    f"{path}: derived baseline forbids source and parent"
                )
        return Workspace(
            mode=_token(value, "mode", path), source=_optional_path(source, path, self.root),
            files=tuple(_relative_list(value, "files", path)),
            editable=tuple(_relative_list(value, "editable", path)),
            build=_workspace_build(value["build"], path), baseline=baseline, parent=parent,
            additions=_optional_path(additions, path, self.root),
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
        expected = VERIFICATION_FIELDS | ({"cases"} if "cases" in value else set())
        _exact_fields(value, expected, path / "verification")
        timeout = value["timeout_seconds"]
        if isinstance(timeout, bool) or not isinstance(timeout, int) or timeout <= 0:
            raise CatalogError(f"{path}: verification timeout must be a positive integer")
        cleanup = value["cleanup_required"]
        if not isinstance(cleanup, bool):
            raise CatalogError(f"{path}: verification cleanup must be boolean")
        raw_cases = value.get("cases", ())
        if "cases" in value and not isinstance(raw_cases, list):
            raise CatalogError(f"{path}: verification cases must be an array of tables")
        cases = tuple(
            self._verification_case(case, path, index)
            for index, case in enumerate(raw_cases)
        )
        if "cases" in value and not cases:
            raise CatalogError(f"{path}: verification cases must be non-empty")
        case_ids = [case.id for case in cases]
        case_labels = [case.label.casefold() for case in cases]
        if len(case_ids) != len(set(case_ids)):
            raise CatalogError(f"{path}: duplicate verification case ids")
        if len(case_labels) != len(set(case_labels)):
            raise CatalogError(f"{path}: duplicate verification case labels")
        if cases:
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
        return Verification(timeout, tuple(_token_list(value, "capabilities", path)),
                            tuple(_token_list(value, "tools", path)), cleanup, cases)

    def _verification_case(self, value, path, index):
        case_path = path / "verification" / "cases" / str(index)
        if not isinstance(value, dict):
            raise CatalogError(f"{case_path}: verification case must be a table")
        _exact_fields(value, VERIFICATION_CASE_FIELDS, case_path)
        case_id = _identifier(value["id"], case_path)
        label = _bounded_printable(value["label"], "label", case_path, empty=False)
        if label != label.strip():
            raise CatalogError(f"{case_path}: label must not have surrounding whitespace")
        variables = value["build_variables"]
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
        check = _optional_path(value["check"], case_path, self.root)
        output = _bounded_printable(
            value["build_output_contains"], "build_output_contains", case_path,
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
        return VerificationCase(
            case_id, label, tuple(sorted(normalized)), expect_build, check, output
        )

    def validate(self) -> None:
        _unique(self.exercises, "id")
        _unique(self.exercises, "path")
        if len(self.required_route) != len(set(self.required_route)):
            raise CatalogError("course.toml: required_route contains duplicates")
        actual = set(self.by_id)
        route = set(self.required_route)
        required = {item.id for item in self.exercises if item.status == "required"}
        if route != required:
            raise CatalogError(f"required_route mismatch (missing={sorted(required-route)}, unknown={sorted(route-required)})")
        if set(self.aliases.values()) != route:
            raise CatalogError("generated serial aliases must cover the required route")
        self._validate_cumulative_route()
        self._validate_elective_tracks(actual)
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
        capability_ids = {item.id for item in self.capabilities}
        tool_ids = {item.id for item in self.tools}
        _unique_registry(self.assumptions, "concept", "competency", "assumptions")
        _unique_registry(self.sections, "id", "label", "sections")
        _unique_registry(self.concepts, "id", "label", "concepts")
        _unique_registry(self.tradecraft_tags, "id", "label", "tradecraft tags")
        _unique_registry(self.capabilities, "id", "label", "capabilities")
        _unique_registry(self.tools, "id", "binary", "tools")
        teachers = {}
        route_positions = {exercise_id: i for i, exercise_id in enumerate(self.required_route)}
        for item in self.exercises:
            if item.stage not in section_ids or item.kind not in KINDS or item.status not in STATUSES:
                raise CatalogError(f"{item.path}: invalid stage, kind, or status")
            if item.serial.startswith("L") and item.kind != "lesson":
                raise CatalogError(f"{item.path}: L alias requires kind='lesson'")
            if item.serial.startswith("C") and item.kind != "capstone":
                raise CatalogError(f"{item.path}: C alias requires kind='capstone'")
            if item.serial.startswith("M") and item.kind != "milestone":
                raise CatalogError(f"{item.path}: M alias requires kind='milestone'")
            if set(item.tradecraft) - tradecraft_ids or item.difficulty not in DIFFICULTIES:
                raise CatalogError(f"{item.path}: invalid tradecraft tag or difficulty")
            if item.workspace.mode not in WORKSPACE_MODES or item.workspace.baseline not in BASELINES:
                raise CatalogError(f"{item.path}: invalid workspace mode or baseline")
            if set(item.verification.capabilities) - capability_ids:
                raise CatalogError(f"{item.path}: unknown VM capabilities")
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
                if item.id in route_positions and route_positions.get(dependency, -1) >= route_positions[item.id]:
                    raise CatalogError(f"{item.path}: prerequisite {dependency!r} is not earlier in required_route")
            provider = self.baseline_provider(item)
            if provider:
                if item.workspace.baseline == "completion":
                    if (
                        item.id not in route_positions
                        or provider.id not in route_positions
                        or route_positions[provider.id] >= route_positions[item.id]
                    ):
                        raise CatalogError(
                            f"{item.path}: completion parent must be earlier in required_route"
                        )
                self.validate_workspace_lineage(item, provider)
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
        used_capabilities = {value for item in self.exercises for value in item.verification.capabilities}
        if used_capabilities != capability_ids:
            raise CatalogError(f"course.toml: capability registry usage mismatch: {sorted(capability_ids-used_capabilities)}")
        used_tools = {value for item in self.exercises for value in item.verification.tools}
        if used_tools != tool_ids:
            raise CatalogError(f"course.toml: tool registry usage mismatch: {sorted(tool_ids-used_tools)}")

    def _validate_elective_tracks(self, exercise_ids: set[str]) -> None:
        _unique_registry(
            self.elective_tracks,
            "id",
            "label",
            "elective track",
        )
        if set(self.track_aliases.values()) != {
            track.id for track in self.elective_tracks
        }:
            raise CatalogError("generated track aliases must cover elective tracks")
        track_members = [
            exercise_id
            for track in self.elective_tracks
            for exercise_id in track.members
        ]
        if len(track_members) != len(set(track_members)):
            raise CatalogError(
                "course.toml: an exercise may belong to only one elective track"
            )
        for track in self.elective_tracks:
            if track.id in exercise_ids:
                raise CatalogError(
                    f"course.toml: elective track ID conflicts with exercise ID: {track.id}"
                )
            for exercise_id in track.members:
                item = self.by_id.get(exercise_id)
                if item is None:
                    raise CatalogError(
                        f"course.toml: elective track {track.id!r} contains unknown "
                        f"exercise {exercise_id!r}"
                    )
                if item.status != "elective":
                    raise CatalogError(
                        f"course.toml: elective track {track.id!r} member "
                        f"{exercise_id!r} must have status='elective'"
                    )
        electives = {
            item.id for item in self.exercises if item.status == "elective"
        }
        if set(track_members) != electives:
            raise CatalogError(
                "course.toml: elective track membership mismatch "
                f"(missing={sorted(electives-set(track_members))}, "
                f"unknown={sorted(set(track_members)-electives)})"
            )

    def _validate_cumulative_route(self) -> None:
        """Validate the one declared cumulative project lineage, when present."""
        milestones = [
            self.by_id[exercise_id]
            for exercise_id in self.required_route
            if self.by_id[exercise_id].kind == "milestone"
        ]
        capstones = [
            self.by_id[exercise_id]
            for exercise_id in self.required_route
            if self.by_id[exercise_id].kind == "capstone"
        ]
        if not self.cumulative_root:
            if any(
                item.workspace.baseline == "cumulative-root"
                for item in self.exercises
            ):
                raise CatalogError(
                    "course.toml: cumulative-root baseline requires cumulative_root"
                )
            return
        if self.cumulative_root not in self.required_route:
            raise CatalogError(
                "course.toml: cumulative_root must name a required-route exercise"
            )
        root = self.by_id[self.cumulative_root]
        if root.kind != "lesson" or root.workspace.mode != "cumulative":
            raise CatalogError(
                "course.toml: cumulative_root must name a cumulative lesson"
            )
        if len(milestones) != 4 or len(capstones) != 1:
            raise CatalogError(
                "course.toml: cumulative route requires exactly four milestones "
                "and one capstone"
            )
        root_position = self.required_route.index(root.id)
        first_position = self.required_route.index(milestones[0].id)
        if root_position >= first_position:
            raise CatalogError(
                "course.toml: cumulative_root must precede the first milestone"
            )
        if milestones[0].workspace.baseline != "cumulative-root":
            raise CatalogError(
                f"{milestones[0].path}: first milestone must use cumulative-root"
            )
        for item in milestones[1:]:
            if item.workspace.baseline != "previous-milestone":
                raise CatalogError(
                    f"{item.path}: later milestone must use previous-milestone"
                )
        if capstones[0].workspace.baseline != "previous-milestone":
            raise CatalogError(
                f"{capstones[0].path}: final capstone must use previous-milestone"
            )
        allowed = {item.id for item in milestones} | {capstones[0].id}
        for item in self.exercises:
            if (
                item.workspace.baseline == "cumulative-root"
                and item.id not in allowed
            ):
                raise CatalogError(
                    f"{item.path}: cumulative-root is reserved for the first milestone"
                )
    def _validate_paths(self, item):
        required_files = [item.resources.readme, item.resources.hint]
        if not item.verification.cases:
            required_files.append(item.resources.check)
        optional_files = [item.resources.host_check]
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
        if item.workspace.source and not (self.root / item.workspace.source).is_dir():
            raise CatalogError(f"{item.path}: workspace source must be an existing directory")
        if item.workspace.source:
            source = self.root / item.workspace.source
            for relative in item.workspace.files:
                if not (source / relative).is_file():
                    raise CatalogError(f"{item.path}: workspace file missing from source: {relative}")
            build = source if item.workspace.build == "." else source / item.workspace.build
            if not build.is_dir():
                raise CatalogError(f"{item.path}: workspace build directory is missing")
        if item.workspace.additions:
            additions = self.root / item.workspace.additions
            if additions.is_symlink() or not additions.is_dir():
                raise CatalogError(
                    f"{item.path}: workspace additions must be a real directory"
                )
        if not set(item.workspace.editable) <= set(item.workspace.files):
            raise CatalogError(f"{item.path}: editable paths must be listed in workspace files")
        if not item.verification.cleanup_required:
            raise CatalogError(f"{item.path}: verification must require cleanup")

    def _validate_dag(self):
        visiting, visited = set(), set()
        def visit(exercise_id):
            if exercise_id in visiting:
                raise CatalogError(f"dependency cycle at {exercise_id}")
            if exercise_id in visited:
                return
            visiting.add(exercise_id)
            item = self.by_id[exercise_id]
            dependencies = list(self.completion_dependencies(item))
            for parent in dependencies:
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

    def ordered(self) -> list[Exercise]:
        """Return the required route in canonical order (compatibility API)."""
        return list(self.required())

    def required(self) -> tuple[Exercise, ...]:
        return tuple(self.by_id[value] for value in self.required_route)

    def track_for(self, item: Exercise) -> ElectiveTrack | None:
        if not item.track_id:
            return None
        return next(
            track for track in self.elective_tracks if track.id == item.track_id
        )

    def display_order(self) -> tuple[Exercise, ...]:
        return self.required() + tuple(
            self.by_id[exercise_id]
            for track in self.elective_tracks
            for exercise_id in track.members
        )

    def module_artifact(self, item: Exercise) -> str:
        """Resolve the module output name from its sparse workspace contract."""
        if item.workspace.module_artifact:
            return item.workspace.module_artifact
        provider = self.baseline_provider(item)
        if provider is not None:
            return self.module_artifact(provider)
        return self.default_module_artifact

    def solution_tree(
        self,
        item: Exercise,
        cache: dict[str, SolutionTree] | None = None,
    ) -> SolutionTree:
        """Return the recursively composed, validated reference solution."""
        cache = {} if cache is None else cache
        if item.id in cache:
            return dict(cache[item.id])
        solution = self.root / item.resources.solution
        provider = self.baseline_provider(item)
        if provider is None or item.workspace.baseline == "completion":
            # Migration boundary: v2's six explicit-parent completions still
            # store full trees. Delete this branch with those assets during the
            # atomic 65-to-target route migration. New derived baselines never
            # use it.
            result = full_solution_tree(solution, item.workspace.files)
        else:
            result = overlay_solution_tree(
                self.solution_tree(provider, cache),
                solution,
                provider.workspace.files,
                item.workspace.files,
            )
        cache[item.id] = result
        return dict(result)

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

    def baseline_provider(self, item: Exercise) -> Exercise | None:
        """Derive the sole workspace inheritance provider, if any."""
        baseline = item.workspace.baseline
        if baseline == "template":
            return None
        if baseline == "completion":
            try:
                return self.by_id[item.workspace.parent]
            except KeyError as exc:
                raise CatalogError(
                    f"{item.path}: unknown completion parent {item.workspace.parent!r}"
                ) from exc
        if baseline == "cumulative-root":
            if item.id not in self.required_route:
                raise CatalogError(
                    f"{item.path}: cumulative-root is only valid on the required route"
                )
            if not self.cumulative_root:
                raise CatalogError(
                    f"{item.path}: course has no cumulative_root"
                )
            try:
                provider = self.by_id[self.cumulative_root]
            except KeyError as exc:
                raise CatalogError(
                    f"course.toml: unknown cumulative_root {self.cumulative_root!r}"
                ) from exc
            if self.required_route.index(provider.id) >= self.required_route.index(item.id):
                raise CatalogError(
                    f"{item.path}: cumulative_root must be earlier in required_route"
                )
            return provider
        if item.id not in self.required_route:
            raise CatalogError(
                f"{item.path}: {baseline} is only valid on the required route"
            )
        position = self.required_route.index(item.id)
        earlier = self.required_route[:position]
        if baseline == "previous-required":
            if not earlier:
                raise CatalogError(f"{item.path}: no previous required baseline provider")
            return self.by_id[earlier[-1]]
        if baseline == "previous-milestone":
            if item.kind not in {"milestone", "capstone"}:
                raise CatalogError(
                    f"{item.path}: previous-milestone requires milestone or capstone kind"
                )
            for stable_id in reversed(earlier):
                candidate = self.by_id[stable_id]
                if candidate.kind == "milestone":
                    return candidate
            raise CatalogError(f"{item.path}: no previous milestone baseline provider")
        raise CatalogError(f"{item.path}: unknown workspace baseline {baseline!r}")

    def completion_dependencies(self, item: Exercise) -> tuple[str, ...]:
        """Return prerequisites plus lineage needed to credit completion."""
        result = list(item.prerequisites)
        provider = self.baseline_provider(item)
        if provider and provider.id not in result:
            result.append(provider.id)
        return tuple(result)

    def validate_workspace_lineage(
        self,
        item: Exercise,
        provider: Exercise | None = None,
    ) -> None:
        """Validate inherited workspace shape against its derived provider."""
        provider = provider or self.baseline_provider(item)
        if provider is None:
            return
        if item.workspace.mode != "cumulative":
            raise CatalogError(
                f"{item.path}: inherited baseline requires cumulative workspace mode"
            )
        if (
            item.workspace.mode != provider.workspace.mode
            or item.workspace.build != provider.workspace.build
        ):
            raise CatalogError(
                f"{item.path}: inherited workspace mode/build must match provider "
                f"{provider.id!r} exactly"
            )
        provider_files = set(provider.workspace.files)
        consumer_files = set(item.workspace.files)
        if not provider_files <= consumer_files:
            removed = sorted(provider_files - consumer_files)
            raise CatalogError(
                f"{item.path}: inherited workspace cannot remove or rename provider "
                f"files: {removed}"
            )
        new_files = consumer_files - provider_files
        additions = item.workspace.additions
        if (
            item.workspace.module_artifact
            and item.workspace.module_artifact != self.module_artifact(provider)
        ):
            raise CatalogError(
                f"{item.path}: inherited workspace module artifact conflicts with "
                f"provider {provider.id!r}"
            )
        if new_files and not additions:
            raise CatalogError(
                f"{item.path}: inherited workspace adds files but has no additions directory"
            )
        if not new_files and additions:
            raise CatalogError(
                f"{item.path}: workspace additions are forbidden when no files are added"
            )
        if additions:
            additions_root = self.root / additions
            if additions_root.is_symlink() or not additions_root.is_dir():
                raise CatalogError(
                    f"{item.path}: workspace additions must be an existing real directory"
                )
            actual = _regular_tree(additions_root, item.path, new_files)
            if actual != new_files:
                raise CatalogError(
                    f"{item.path}: additions directory mismatch "
                    f"(missing={sorted(new_files-actual)}, unknown={sorted(actual-new_files)})"
                )

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

    def verification_cases(self, item: Exercise) -> tuple[VerificationCase, ...]:
        """Return explicit cases, or the compatible single success case."""
        if item.verification.cases:
            return item.verification.cases
        return (VerificationCase(
            "default", "Default", (), "pass", item.resources.check, ""
        ),)

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


def _rows(data, key, fields, cls, id_field, *, allow_empty=False):
    value = data[key]
    if not isinstance(value, list):
        raise CatalogError(f"course.toml: {key} must be an array of tables")
    if not value and not allow_empty:
        raise CatalogError(f"course.toml: {key} must be non-empty")
    result = []
    for index, row in enumerate(value):
        _exact_fields(row, fields, Path(f"course.toml/{key}/{index}"))
        ident, label = row[id_field], row[next(v for v in fields if v != id_field)]
        if not isinstance(ident, str) or not TOKEN_RE.fullmatch(ident) or not isinstance(label, str) or not label.strip():
            raise CatalogError(f"course.toml: invalid {key} row {index}")
        result.append(cls(ident, label.strip()))
    return result


def _elective_track_rows(data) -> list[ElectiveTrack]:
    value = data.get("elective_tracks", [])
    if not isinstance(value, list):
        raise CatalogError("course.toml: elective_tracks must be an array of tables")
    result = []
    for index, row in enumerate(value):
        path = Path(f"course.toml/elective_tracks/{index}")
        if not isinstance(row, dict):
            raise CatalogError(f"{path}: elective track must be a table")
        _exact_fields(row, {"id", "label", "members"}, path)
        track_id = _identifier(row["id"], path)
        label = _bounded_printable(row["label"], "label", path, empty=False)
        if label != label.strip():
            raise CatalogError(f"{path}: label must not have surrounding whitespace")
        members = tuple(_id_list(row, "members", path))
        if not members:
            raise CatalogError(f"{path}: members must not be empty")
        if len(members) != len(set(members)):
            raise CatalogError(f"{path}: duplicate values in members")
        result.append(ElectiveTrack(track_id, label, members))
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
        candidate = PurePosixPath(value)
        if candidate.is_absolute() or not candidate.parts or any(part in {"", ".", ".."} for part in candidate.parts):
            raise CatalogError(f"{path}: unsafe workspace-relative path {value!r}")
        result.append(candidate.as_posix())
    if len(result) != len(set(result)):
        raise CatalogError(f"{path}: duplicate paths in {key}")
    return result


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


def _regular_tree(
    root: Path,
    owner: Path,
    expected_files: set[str],
) -> set[str]:
    """Return an additions tree's files, rejecting links and special entries."""
    files: set[str] = set()
    directories: set[str] = set()
    for directory, names, filenames in os.walk(root, followlinks=False):
        directory_path = Path(directory)
        for name in names:
            path = directory_path / name
            if path.is_symlink() or not path.is_dir():
                raise CatalogError(
                    f"{owner}: workspace additions contain a symlink or special directory"
                )
            directories.add(path.relative_to(root).as_posix())
        for name in filenames:
            path = directory_path / name
            if path.is_symlink() or not path.is_file():
                raise CatalogError(
                    f"{owner}: workspace additions contain a symlink or special file"
                )
            files.add(path.relative_to(root).as_posix())
    expected_directories = {
        parent.as_posix()
        for value in expected_files
        for parent in PurePosixPath(value).parents
        if parent != PurePosixPath(".")
    }
    if directories != expected_directories:
        raise CatalogError(
            f"{owner}: additions directory contains stray or empty directories"
        )
    return files


def _unique(items, field):
    values = [getattr(item, field) for item in items]
    if len(values) != len(set(values)): raise CatalogError(f"duplicate catalog {field}")
