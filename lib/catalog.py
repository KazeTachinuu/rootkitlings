"""Dependency-free loader and validator for the rootkitlings catalog v2."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path, PurePosixPath
import re
import tomllib


ID_RE = re.compile(r"[a-z0-9][a-z0-9-]*\Z")
SERIAL_RE = re.compile(r"(?:L|M|C)[0-9]{2}\Z")
TOKEN_RE = re.compile(r"[a-z0-9][a-z0-9-]*\Z")
DIFFICULTIES = {"intro", "standard", "advanced", "challenge"}
KINDS = {"lesson", "milestone", "capstone"}
STATUSES = {"required", "elective", "retired"}
WORKSPACE_MODES = {"cumulative", "single-file", "project"}
BASELINES = {"template", "completion"}
TOP_FIELDS = {
    "schema_version", "id", "title", "stage", "kind", "status",
    "tradecraft", "difficulty", "prerequisites", "teaches", "requires",
    "workspace", "resources", "verification",
}
WORKSPACE_FIELDS = {"mode", "source", "files", "editable", "build", "baseline", "parent"}
RESOURCE_FIELDS = {"readme", "check", "host_check", "hint", "solution"}
VERIFICATION_FIELDS = {"timeout_seconds", "capabilities", "tools", "cleanup_required"}
QUERY_FIELDS = {
    "id", "path", "serial", "sequence", "title", "stage", "kind",
    "status", "tradecraft", "difficulty", "prerequisites", "teaches", "requires",
    "workspace_mode", "workspace_source", "workspace_files", "workspace_editable",
    "workspace_build", "workspace_baseline", "workspace_parent", "readme", "check",
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
class Workspace:
    mode: str
    source: str
    files: tuple[str, ...]
    editable: tuple[str, ...]
    build: str
    baseline: str
    parent: str


@dataclass(frozen=True)
class Resources:
    readme: str
    check: str
    host_check: str
    hint: str
    solution: str


@dataclass(frozen=True)
class Verification:
    timeout_seconds: int
    capabilities: tuple[str, ...]
    tools: tuple[str, ...]
    cleanup_required: bool


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

    def value(self, field: str) -> object:
        if field not in QUERY_FIELDS:
            raise CatalogError(f"unknown field: {field}")
        values = {
            "id": self.id, "path": self.path.as_posix(), "serial": self.serial,
            "sequence": self.sequence, "title": self.title, "stage": self.stage,
            "kind": self.kind, "status": self.status,
            "tradecraft": self.tradecraft, "difficulty": self.difficulty,
            "prerequisites": self.prerequisites, "teaches": self.teaches,
            "requires": self.requires, "workspace_mode": self.workspace.mode,
            "workspace_source": self.workspace.source, "workspace_files": self.workspace.files,
            "workspace_editable": self.workspace.editable,
            "workspace_build": self.workspace.build,
            "workspace_baseline": self.workspace.baseline,
            "workspace_parent": self.workspace.parent, "readme": self.resources.readme,
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
        (self.revision, self.exercise_roots, self.required_route, self.aliases, self.tradecraft_tags,
         self.capabilities, self.tools, self.assumptions, self.sections,
         self.concepts) = self._read_course()
        self.concept_by_id = {item.id: item for item in self.concepts}
        raw = self._read_exercises()
        self.by_id = {item.id: item for item in raw}
        reverse_alias = {exercise_id: alias for alias, exercise_id in self.aliases.items()}
        positions = {exercise_id: position for position, exercise_id in enumerate(self.required_route)}
        self.exercises = [
            Exercise(**{**item.__dict__, "serial": reverse_alias.get(item.id, ""),
                        "sequence": positions.get(item.id, -1)}) for item in raw
        ]
        self.by_id = {item.id: item for item in self.exercises}
        self.validate()

    def _read_course(self):
        file = self.root / "course.toml"
        try:
            data = tomllib.loads(file.read_text(encoding="utf-8"))
        except (OSError, tomllib.TOMLDecodeError) as exc:
            raise CatalogError(f"cannot parse course.toml: {exc}") from exc
        allowed = {
            "schema_version", "revision", "exercise_roots", "required_route",
            "aliases", "tradecraft_tags", "capabilities", "tools", "assumptions",
            "sections", "concepts",
        }
        _exact_fields(data, allowed, Path("course.toml"))
        if data["schema_version"] != 2 or isinstance(data["schema_version"], bool):
            raise CatalogError("course.toml: schema_version must be integer 2")
        revision = _string(data, "revision", Path("course.toml"))
        roots = tuple(_path_list(data, "exercise_roots", Path("course.toml"), self.root))
        for root in roots:
            if not (self.root / root).is_dir():
                raise CatalogError(f"course.toml: exercise root must be a directory: {root}")
        route = tuple(_id_list(data, "required_route", Path("course.toml")))
        aliases = data["aliases"]
        if not isinstance(aliases, dict) or not aliases:
            raise CatalogError("course.toml: aliases must be a non-empty table")
        for alias, exercise_id in aliases.items():
            if not SERIAL_RE.fullmatch(alias) or not isinstance(exercise_id, str) or not ID_RE.fullmatch(exercise_id):
                raise CatalogError(f"course.toml: invalid alias {alias!r} = {exercise_id!r}")
        assumptions = tuple(_rows(data, "assumptions", {"concept", "competency"}, Assumption, "concept"))
        sections = tuple(_rows(data, "sections", {"id", "label"}, Section, "id"))
        concepts = tuple(_rows(data, "concepts", {"id", "label"}, Concept, "id"))
        tradecraft_tags = tuple(_rows(data, "tradecraft_tags", {"id", "label"}, Concept, "id"))
        capabilities = tuple(_rows(data, "capabilities", {"id", "label"}, Concept, "id"))
        tools = tuple(_tool_rows(data, self.root))
        return (revision, roots, route, dict(aliases), tradecraft_tags, capabilities, tools,
                assumptions, sections, concepts)

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
        expected = common | ({"source"} if baseline == "template" else {"parent"})
        _exact_fields(value, expected, path / "workspace")
        source = value.get("source", "")
        parent = value.get("parent", "")
        if baseline == "template":
            if not source or parent:
                raise CatalogError(f"{path}: template baseline requires source and forbids parent")
        elif baseline == "completion":
            if source or not parent:
                raise CatalogError(f"{path}: completion baseline requires parent and forbids source")
            _identifier(parent, path)
        return Workspace(
            mode=_token(value, "mode", path), source=_optional_path(source, path, self.root),
            files=tuple(_relative_list(value, "files", path)),
            editable=tuple(_relative_list(value, "editable", path)),
            build=_workspace_build(value["build"], path), baseline=baseline, parent=parent,
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
        cleanup = value["cleanup_required"]
        if not isinstance(cleanup, bool):
            raise CatalogError(f"{path}: verification cleanup must be boolean")
        return Verification(timeout, tuple(_token_list(value, "capabilities", path)),
                            tuple(_token_list(value, "tools", path)), cleanup)

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
        alias_values = list(self.aliases.values())
        if len(alias_values) != len(set(alias_values)) or set(alias_values) != actual:
            raise CatalogError("course.toml: serial aliases must cover every exercise exactly once")
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
            if item.workspace.parent:
                if item.workspace.parent not in actual or item.workspace.parent == item.id:
                    raise CatalogError(f"{item.path}: invalid workspace parent")
                if route_positions.get(item.workspace.parent, -1) >= route_positions.get(item.id, -1):
                    raise CatalogError(f"{item.path}: completion parent must be earlier in required_route")
                parent_workspace = self.by_id[item.workspace.parent].workspace
                if (item.workspace.files != parent_workspace.files or
                        item.workspace.build != parent_workspace.build):
                    raise CatalogError(
                        f"{item.path}: completion workspace files/build must match parent exactly"
                    )
            self._validate_paths(item)
        self._validate_dag()
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

    def _validate_paths(self, item):
        required_files = [item.resources.readme, item.resources.check, item.resources.hint]
        optional_files = [item.resources.host_check]
        for value in required_files:
            if not value or not (self.root / value).is_file():
                raise CatalogError(f"{item.path}: missing resource file: {value!r}")
        for value in optional_files:
            if value and not (self.root / value).is_file():
                raise CatalogError(f"{item.path}: missing optional resource file: {value}")
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
            dependencies = list(item.prerequisites)
            if item.workspace.parent:
                dependencies.append(item.workspace.parent)
            for parent in dependencies:
                visit(parent)
            visiting.remove(exercise_id); visited.add(exercise_id)
        for exercise_id in self.by_id:
            visit(exercise_id)

    def resolve(self, selector: str) -> Exercise:
        exercise_id = self.aliases.get(selector, selector)
        try:
            return self.by_id[exercise_id]
        except KeyError as exc:
            raise CatalogError(f"unknown exercise: {selector}") from exc

    def ordered(self) -> list[Exercise]:
        return [self.by_id[value] for value in self.required_route]

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
    if not isinstance(value, list) or not value: raise CatalogError(f"course.toml: {key} must be non-empty")
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


def _unique(items, field):
    values = [getattr(item, field) for item in items]
    if len(values) != len(set(values)): raise CatalogError(f"duplicate catalog {field}")
