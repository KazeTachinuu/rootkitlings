"""Dependency-free loader and validator for the rootkitlings catalog."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path, PurePosixPath
import re
import tomllib


SERIAL_RE = re.compile(r"(?:L[0-9]{2}|C[0-9]{2})\Z")
TOKEN_RE = re.compile(r"[a-z0-9][a-z0-9-]*\Z")
DIFFICULTIES = {"intro", "standard", "advanced", "challenge"}
VM_CAPABILITIES = {"aes-gcm", "network"}
FIELDS = {
    "schema_version", "serial", "sequence", "title", "difficulty", "prerequisites",
    "teaches", "requires", "vm_capabilities", "edit_paths",
}


class CatalogError(ValueError):
    pass


def serial_text(value: object) -> str:
    if not isinstance(value, str) or not SERIAL_RE.fullmatch(value):
        raise CatalogError(f"serial must be an L00/C00-style string, got {value!r}")
    return value


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
class Exercise:
    path: Path
    serial: str
    sequence: int
    title: str
    difficulty: str
    prerequisites: tuple[str, ...]
    teaches: tuple[str, ...]
    requires: tuple[str, ...]
    vm_capabilities: tuple[str, ...]
    edit_paths: tuple[str, ...]

    def value(self, field: str) -> object:
        if field == "path":
            return self.path.as_posix()
        if field not in FIELDS:
            raise CatalogError(f"unknown field: {field}")
        return 1 if field == "schema_version" else getattr(self, field)


class Catalog:
    def __init__(self, root: Path):
        self.root = root.resolve()
        self.assumptions, self.sections, self.concepts = self._read_course()
        self.concept_by_id = {concept.id: concept for concept in self.concepts}
        self.exercises = self._read_exercises()
        self.by_serial = {item.serial: item for item in self.exercises}
        self.validate()

    def _read_course(self) -> tuple[tuple[Assumption, ...], tuple[Section, ...], tuple[Concept, ...]]:
        file = self.root / "course.toml"
        try:
            data = tomllib.loads(file.read_text(encoding="utf-8"))
        except (OSError, tomllib.TOMLDecodeError) as exc:
            raise CatalogError(f"cannot parse course.toml: {exc}") from exc
        allowed = {"schema_version", "assumptions", "sections", "concepts"}
        unknown = set(data) - allowed
        missing = allowed - set(data)
        if unknown or missing:
            raise CatalogError(f"course.toml fields mismatch (missing={sorted(missing)}, unknown={sorted(unknown)})")
        if data["schema_version"] != 1 or isinstance(data["schema_version"], bool):
            raise CatalogError("course.toml: schema_version must be integer 1")
        rows = data["assumptions"]
        if not isinstance(rows, list) or not rows:
            raise CatalogError("course.toml: assumptions must be a non-empty array of tables")
        assumptions: list[Assumption] = []
        for index, row in enumerate(rows):
            if not isinstance(row, dict) or set(row) != {"concept", "competency"}:
                raise CatalogError(f"course.toml: assumptions[{index}] must contain exactly concept and competency")
            concept = row["concept"]
            competency = row["competency"]
            if not isinstance(concept, str) or not TOKEN_RE.fullmatch(concept):
                raise CatalogError(f"course.toml: invalid assumption concept {concept!r}")
            if not isinstance(competency, str) or not competency.strip():
                raise CatalogError(f"course.toml: empty competency for {concept}")
            assumptions.append(Assumption(concept, competency.strip()))
        section_rows = data["sections"]
        if not isinstance(section_rows, list) or not section_rows:
            raise CatalogError("course.toml: sections must be a non-empty array of tables")
        sections: list[Section] = []
        for index, row in enumerate(section_rows):
            if not isinstance(row, dict) or set(row) != {"id", "label"}:
                raise CatalogError(f"course.toml: sections[{index}] must contain exactly id and label")
            section_id = row["id"]
            label = row["label"]
            if not isinstance(section_id, str) or not TOKEN_RE.fullmatch(section_id):
                raise CatalogError(f"course.toml: invalid section id {section_id!r}")
            if not isinstance(label, str) or not label.strip():
                raise CatalogError(f"course.toml: empty label for section {section_id}")
            sections.append(Section(section_id, label.strip()))
        concept_rows = data["concepts"]
        if not isinstance(concept_rows, list) or not concept_rows:
            raise CatalogError("course.toml: concepts must be a non-empty array of tables")
        concepts: list[Concept] = []
        for index, row in enumerate(concept_rows):
            if not isinstance(row, dict) or set(row) != {"id", "label"}:
                raise CatalogError(f"course.toml: concepts[{index}] must contain exactly id and label")
            concept_id = row["id"]
            label = row["label"]
            if not isinstance(concept_id, str) or not TOKEN_RE.fullmatch(concept_id):
                raise CatalogError(f"course.toml: invalid concept id {concept_id!r}")
            if not isinstance(label, str) or not label.strip():
                raise CatalogError(f"course.toml: empty label for concept {concept_id}")
            label = label.strip()
            if any(delimiter in label for delimiter in (",", "\t", "\n", "\x1f")):
                raise CatalogError(f"course.toml: concept label contains a reserved delimiter: {label!r}")
            concepts.append(Concept(concept_id, label))
        return tuple(assumptions), tuple(sections), tuple(concepts)

    def _read_exercises(self) -> list[Exercise]:
        files = sorted(self.root.glob("lings/*/*/exercise.toml"))
        files += sorted(self.root.glob("capstones/*/exercise.toml"))
        items: list[Exercise] = []
        for file in files:
            relative_file = file.relative_to(self.root)
            try:
                data = tomllib.loads(file.read_text(encoding="utf-8"))
            except (OSError, tomllib.TOMLDecodeError) as exc:
                raise CatalogError(f"cannot parse {relative_file}: {exc}") from exc
            unknown = set(data) - FIELDS
            missing = FIELDS - set(data)
            if unknown:
                raise CatalogError(f"{relative_file}: unknown fields: {', '.join(sorted(unknown))}")
            if missing:
                raise CatalogError(f"{relative_file}: missing fields: {', '.join(sorted(missing))}")
            if data["schema_version"] != 1 or isinstance(data["schema_version"], bool):
                raise CatalogError(f"{relative_file}: schema_version must be integer 1")
            serial = serial_text(data["serial"])
            path = file.parent.relative_to(self.root)
            items.append(Exercise(
                path=path,
                serial=serial,
                sequence=_nonnegative_int(data, "sequence", relative_file),
                title=_string(data, "title", relative_file),
                difficulty=_string(data, "difficulty", relative_file),
                prerequisites=tuple(serial_text(v) for v in _list(data, "prerequisites", relative_file)),
                teaches=tuple(_token_list(data, "teaches", relative_file)),
                requires=tuple(_token_list(data, "requires", relative_file)),
                vm_capabilities=tuple(_token_list(data, "vm_capabilities", relative_file)),
                edit_paths=tuple(_edit_paths(data, "edit_paths", relative_file, self.root)),
            ))
        return items

    def validate(self) -> None:
        if len(self.exercises) != 65:
            raise CatalogError(f"expected 65 exercises, found {len(self.exercises)}")
        exercise_dirs = {
            path.parent.relative_to(self.root)
            for pattern in ("lings/*/*/README.md", "capstones/*/README.md")
            for path in self.root.glob(pattern)
        }
        metadata_dirs = {item.path for item in self.exercises}
        if exercise_dirs != metadata_dirs:
            missing = sorted(path.as_posix() for path in exercise_dirs - metadata_dirs)
            orphaned = sorted(path.as_posix() for path in metadata_dirs - exercise_dirs)
            raise CatalogError(f"exercise metadata/path mismatch (missing={missing}, orphaned={orphaned})")
        _unique(self.exercises, "serial")
        _unique(self.exercises, "sequence")
        _unique(self.exercises, "path")
        baseline = [assumption.concept for assumption in self.assumptions]
        if len(baseline) != len(set(baseline)):
            raise CatalogError("course.toml: duplicate assumption concepts")
        section_ids = [section.id for section in self.sections]
        section_labels = [section.label for section in self.sections]
        if len(section_ids) != len(set(section_ids)) or len(section_labels) != len(set(section_labels)):
            raise CatalogError("course.toml: duplicate section ids or labels")
        concept_ids = [concept.id for concept in self.concepts]
        concept_labels = [concept.label for concept in self.concepts]
        if len(concept_ids) != len(set(concept_ids)) or len(concept_labels) != len(set(concept_labels)):
            raise CatalogError("course.toml: duplicate concept ids or labels")
        directory_sections = {
            path.name.split("_", 1)[-1].replace("_", "-")
            for path in (self.root / "lings").iterdir()
            if path.is_dir()
        }
        if set(section_ids) != directory_sections:
            raise CatalogError(
                f"course.toml: section coverage mismatch "
                f"(missing={sorted(directory_sections - set(section_ids))}, "
                f"unknown={sorted(set(section_ids) - directory_sections)})"
            )
        actual = {item.serial for item in self.exercises}
        sequences = {item.sequence for item in self.exercises}
        if sequences != set(range(len(self.exercises))):
            raise CatalogError("catalog sequences must be contiguous from 0")
        positions = {item.serial: item.sequence for item in self.exercises}
        teachers: dict[str, Exercise] = {}
        for item in self.exercises:
            parts = item.path.parts
            dirname = parts[-1]
            number = int(dirname.split("-", 1)[0])
            expected_serial = f"L{number:02d}" if parts[0] == "lings" else f"C{number:02d}"
            if item.serial != expected_serial:
                raise CatalogError(f"{item.path}: serial must be {expected_serial!r}")
            if item.difficulty not in DIFFICULTIES:
                raise CatalogError(f"{item.path}: invalid difficulty {item.difficulty!r}")
            unknown_caps = set(item.vm_capabilities) - VM_CAPABILITIES
            if unknown_caps:
                raise CatalogError(f"{item.path}: unknown VM capabilities: {sorted(unknown_caps)}")
            if not item.teaches:
                raise CatalogError(f"{item.path}: teaches must not be empty")
            if len(item.teaches) != len(set(item.teaches)) or len(item.requires) != len(set(item.requires)):
                raise CatalogError(f"{item.path}: duplicate concepts")
            overlap = set(item.teaches) & set(item.requires)
            if overlap:
                raise CatalogError(f"{item.path}: concepts cannot be both taught and required: {sorted(overlap)}")
            for concept in item.teaches:
                if concept in teachers:
                    raise CatalogError(f"concept {concept!r} is taught by both {teachers[concept].path} and {item.path}")
                teachers[concept] = item
            if len(item.edit_paths) != len(set(item.edit_paths)):
                raise CatalogError(f"{item.path}: duplicate edit paths")
            if item.path.parts[0] == "capstones":
                for edit_path in item.edit_paths:
                    if not (self.root / edit_path).is_file():
                        raise CatalogError(f"{item.path}: capstone edit path does not exist: {edit_path}")
            for prerequisite in item.prerequisites:
                if prerequisite not in actual:
                    raise CatalogError(f"{item.path}: unknown prerequisite {prerequisite!r}")
                if positions[prerequisite] >= item.sequence:
                    raise CatalogError(f"{item.path}: prerequisite {prerequisite!r} is not earlier in the sequence")
        baseline_concepts = set(baseline)
        for item in self.exercises:
            unknown = set(item.requires) - (baseline_concepts | self.prerequisite_concepts(item))
            if unknown:
                raise CatalogError(f"{item.path}: required concepts are not taught through prerequisites: {sorted(unknown)}")
            for prerequisite in item.prerequisites:
                parent = self.by_serial[prerequisite]
                parent_concepts = set(parent.teaches) | self.prerequisite_concepts(parent)
                other_concepts = {
                    concept
                    for other in item.prerequisites
                    if other != prerequisite
                    for concept in (
                        *self.by_serial[other].teaches,
                        *self.prerequisite_concepts(self.by_serial[other]),
                    )
                }
                contribution = (parent_concepts & set(item.requires)) - other_concepts
                if not contribution:
                    raise CatalogError(
                        f"{item.path}: prerequisite {prerequisite!r} contributes no unique required concept"
                    )
                reachable_through_others = {
                    ancestor.serial
                    for other in item.prerequisites
                    if other != prerequisite
                    for ancestor in (
                        self.by_serial[other],
                        *self.prerequisite_exercises(self.by_serial[other]),
                    )
                }
                if prerequisite in reachable_through_others:
                    raise CatalogError(
                        f"{item.path}: prerequisite {prerequisite!r} is already supplied "
                        "through another direct prerequisite"
                    )
        overlap = set(baseline) & set(teachers)
        if overlap:
            raise CatalogError(f"baseline concepts must not also be taught by exercises: {sorted(overlap)}")
        used_concepts = set(baseline) | set(teachers) | {
            concept for item in self.exercises for concept in item.requires
        }
        registered_concepts = set(concept_ids)
        if registered_concepts != used_concepts:
            raise CatalogError(
                f"course.toml: concept coverage mismatch "
                f"(missing={sorted(used_concepts - registered_concepts)}, "
                f"unknown={sorted(registered_concepts - used_concepts)})"
            )

    def resolve(self, selector: str) -> Exercise:
        if selector not in self.by_serial:
            raise CatalogError(f"unknown exercise: {selector}")
        return self.by_serial[selector]

    def ordered(self) -> list[Exercise]:
        return sorted(self.exercises, key=lambda item: item.sequence)

    def prerequisite_exercises(self, item: Exercise) -> tuple[Exercise, ...]:
        seen: set[str] = set()

        def visit(serial: str) -> None:
            if serial in seen:
                return
            seen.add(serial)
            for parent in self.by_serial[serial].prerequisites:
                visit(parent)

        for serial in item.prerequisites:
            visit(serial)
        return tuple(sorted((self.by_serial[serial] for serial in seen), key=lambda value: value.sequence))

    def prerequisite_concepts(self, item: Exercise) -> set[str]:
        return {concept for exercise in self.prerequisite_exercises(item) for concept in exercise.teaches}

    def all_concepts(self, item: Exercise) -> set[str]:
        return {assumption.concept for assumption in self.assumptions} | self.prerequisite_concepts(item)

    def assumption(self, concept_id: str) -> Assumption | None:
        return next((item for item in self.assumptions if item.concept == concept_id), None)

    def concept_label(self, concept_id: str) -> str:
        try:
            return self.concept_by_id[concept_id].label
        except KeyError as exc:
            raise CatalogError(f"unknown concept: {concept_id}") from exc

    def section(self, item: Exercise) -> str:
        """Return the canonical display label for an exercise's section."""
        if item.path.parts[0] == "lings":
            section_id = item.path.parts[1].split("_", 1)[-1].replace("_", "-")
        else:
            previous_lings = [
                candidate for candidate in self.exercises
                if candidate.path.parts[0] == "lings" and candidate.sequence < item.sequence
            ]
            if not previous_lings:
                raise CatalogError(f"{item.path}: capstone has no preceding ling section")
            previous = max(previous_lings, key=lambda candidate: candidate.sequence)
            section_id = previous.path.parts[1].split("_", 1)[-1].replace("_", "-")
        return next(section.label for section in self.sections if section.id == section_id)

    def workspace_mode(self, item: Exercise) -> str:
        """Describe how the CLI selects and builds an exercise workspace."""
        directory = self.root / item.path
        if (directory / "Kbuild").is_file():
            return "project"
        if (directory / "hello.c").is_file():
            return "single-file"
        return "cumulative"

    def solution_path(self, item: Exercise) -> str:
        """Return the reference implementation path used by the course."""
        name = item.path.name
        directory = self.root / ".rootkitlings" / "solutions" / name
        if directory.is_dir():
            return directory.relative_to(self.root).as_posix()
        return (Path(".rootkitlings") / "solutions" / f"{name}.c").as_posix()


def _string(data: dict[str, object], key: str, path: Path) -> str:
    value = data[key]
    if not isinstance(value, str) or not value.strip():
        raise CatalogError(f"{path}: {key} must be a non-empty string")
    if key == "difficulty" and not TOKEN_RE.fullmatch(value):
        raise CatalogError(f"{path}: invalid {key} token {value!r}")
    return value


def _list(data: dict[str, object], key: str, path: Path) -> list[object]:
    value = data[key]
    if not isinstance(value, list):
        raise CatalogError(f"{path}: {key} must be an array")
    return value


def _token_list(data: dict[str, object], key: str, path: Path) -> list[str]:
    values = _list(data, key, path)
    if any(not isinstance(value, str) or not TOKEN_RE.fullmatch(value) for value in values):
        raise CatalogError(f"{path}: {key} must contain lowercase slug tokens")
    return values


def _nonnegative_int(data: dict[str, object], key: str, path: Path) -> int:
    value = data[key]
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise CatalogError(f"{path}: {key} must be a non-negative integer")
    return value


def _edit_paths(data: dict[str, object], key: str, path: Path, root: Path) -> list[str]:
    values = _list(data, key, path)
    if not values:
        raise CatalogError(f"{path}: edit_paths must not be empty")
    result: list[str] = []
    for value in values:
        if not isinstance(value, str):
            raise CatalogError(f"{path}: edit_paths must contain strings")
        candidate = PurePosixPath(value)
        if candidate.is_absolute() or not candidate.parts or any(part in {"", ".", ".."} for part in candidate.parts):
            raise CatalogError(f"{path}: unsafe edit path {value!r}")
        if not (root / candidate).parent.is_dir():
            raise CatalogError(f"{path}: edit path parent does not exist: {value}")
        result.append(candidate.as_posix())
    return result


def _unique(items: list[Exercise], field: str) -> None:
    values = [getattr(item, field) for item in items]
    if len(values) != len(set(values)):
        raise CatalogError(f"duplicate catalog {field}")
