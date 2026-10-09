#!/usr/bin/env python3
"""Rootkitlings learner CLI."""

from __future__ import annotations

import difflib
import os
import shutil
import sys
from pathlib import Path

from engine.catalog import Catalog, CatalogError
from engine.build_env import module_build_environment
from engine.console import Console
from engine.process import ProcessError, ProcessTimeout, run_process
from engine.state import StateStore, StateError
from engine.verification import VerificationError, build_verification_cases
from engine.workspace import (
    WorkspaceError,
    archive_and_remove_workspaces,
    canonical_tree_hash,
    create_snapshot,
    materialize,
    materialize_inherited,
    restore_workspace,
    verify_snapshot,
)

ROOT = Path(__file__).resolve().parent.parent
RT = ROOT / '.rootkitlings'
STATE = RT / 'state.toml'
WORK = RT / 'work'
PASSED = RT / 'passed'
ARCHIVE = RT / 'archive'
DOCTOR_TIMEOUT = 30
KERNEL_PREP_TIMEOUT = 3600
SETUP_PROBE_TIMEOUT = 180


class CLIError(RuntimeError):
    def __init__(self, message, recovery=()):
        super().__init__(message)
        self.recovery = tuple(recovery)


UI = Console()


def die(message):
    raise CLIError(message)


try:
    CAT = Catalog(ROOT)
except CatalogError as error:
    UI.error(f'invalid course catalog: {error}')
    raise SystemExit(1)

ROUTE = tuple(CAT.required_route)
COMPLETION_DEPENDENCIES = {
    exercise.id: frozenset(CAT.completion_dependencies(exercise))
    for exercise in CAT.exercises
}
BASELINE_PROVIDERS = {
    exercise.id: (
        provider.id if (provider := CAT.baseline_provider(exercise)) else None
    )
    for exercise in CAT.exercises
}
STORE = StateStore(
    STATE,
    set(CAT.by_id),
    ROUTE,
    CAT.revision,
    COMPLETION_DEPENDENCIES,
    BASELINE_PROVIDERS,
)


def initialize_runtime():
    for path in (RT, WORK, PASSED, ARCHIVE):
        path.mkdir(parents=True, exist_ok=True)
        if path.is_symlink():
            die(f'Runtime directory must not be a symlink: {path.relative_to(ROOT)}')


def course_kernel_ready():
    kernel = run(
        [str(ROOT / 'scripts/kernel'), 'status'],
        timeout=10,
        capture=True,
    )
    return kernel.returncode == 0


def require_setup():
    if not course_kernel_ready():
        die('Course kernel is not ready. Run: ./rootkitlings setup')


def cmd_setup(a):
    none(a, 'setup')
    UI.heading('Prepare course VM')
    UI.progress('Checking host tools')
    doctor = run(
        [str(ROOT / 'scripts/doctor')],
        timeout=DOCTOR_TIMEOUT,
        capture=True,
    )
    if doctor.returncode:
        if doctor.stdout:
            UI.raw_block(doctor.stdout, error=True)
        die('Setup failed: scripts/doctor did not pass')
    UI.success('Host tools ready')
    UI.progress('Preparing the pinned course kernel')
    kernel = run(
        [str(ROOT / 'scripts/kernel'), 'ensure'],
        timeout=KERNEL_PREP_TIMEOUT,
    )
    if kernel.returncode:
        die('Setup failed: course kernel preparation did not pass')
    UI.progress('Checking the disposable guest boundary')
    containment = run(
        [
            str(ROOT / 'vm/boot'),
            '--check',
            str(ROOT / 'vm/checks/containment'),
        ],
        timeout=SETUP_PROBE_TIMEOUT,
        capture=True,
    )
    if containment.returncode:
        if containment.stdout:
            UI.raw_block(containment.stdout, error=True)
        die('Setup failed: disposable guest containment check did not pass')
    UI.success('Guest boundary verified')
    UI.success('Setup ready')
    with STORE.read_locked() as state:
        show_next(state, kernel_ready=True)


def label(item):
    return item.serial or item.id


def kind_label(item):
    return {
        'lesson': 'Lesson',
        'milestone': 'Milestone',
        'capstone': 'Capstone',
    }.get(item.kind, 'Exercise')


def shown(p):
    try:
        return str(Path(p).relative_to(ROOT))
    except ValueError:
        return str(p)


def current(state):
    return CAT.by_id[state.current] if state.current else None


def next_command(state, *, kernel_ready):
    item = current(state)
    if item is None:
        return None
    if not kernel_ready:
        return './rootkitlings setup'
    workspace = wp(item)
    if workspace.is_dir() and not workspace.is_symlink():
        return f'./rootkitlings check {label(item)}'
    return f'./rootkitlings read {label(item)}'


def show_next(state, *, kernel_ready):
    command = next_command(state, kernel_ready=kernel_ready)
    if command is None:
        UI.section('Course complete')
        return
    UI.section('Next')
    UI.command(command)


def resolve(q, state):
    if not q:
        x = current(state)
        if not x:
            die('Course complete; specify an exercise ID or serial')
        return x
    try:
        return CAT.resolve(q)
    except CatalogError as error:
        raise CLIError(str(error)) from error


def available(x, state):
    if x.status == 'required':
        if x.id in state.completed or state.current is None:
            return
        if ROUTE.index(x.id) > ROUTE.index(state.current):
            now = CAT.by_id[state.current]
            die(f'{label(x)} is locked. Complete {label(now)}: {now.title} first.')
    else:
        missing = [p for p in x.prerequisites if p not in state.completed]
        if missing:
            die(f'{label(x)} is locked; incomplete prerequisites: {", ".join(missing)}')


def wp(x):
    return WORK / x.id


def bd(x):
    return wp(x) if x.workspace.build == '.' else wp(x) / x.workspace.build


def need(x):
    p = wp(x)
    if not p.is_dir() or p.is_symlink():
        die(f'{label(x)} has no workspace. Run: ./rootkitlings start {label(x)}')
    return p


def snap(content_hash):
    verify_snapshot(PASSED / content_hash, content_hash)
    return PASSED / content_hash


def baseline(x, state):
    provider = CAT.baseline_provider(x)
    if provider is None:
        return ROOT / x.workspace.source
    r = state.exercises.get(provider.id)
    if not r or not r.passed_snapshot:
        die(f'{label(x)} needs a passed snapshot from {label(provider)}')
    return snap(r.passed_snapshot)


def materialize_workspace(x, state):
    if getattr(x.workspace, 'baseline', '') == 'scaffold':
        return CAT.materialize_starter(x, WORK, x.id)
    source = baseline(x, state)
    provider = CAT.baseline_provider(x)
    if provider is not None and x.workspace.additions:
        return materialize_inherited(
            source,
            ROOT / x.workspace.additions,
            WORK,
            x.id,
            provider.workspace.files,
            x.workspace.files,
        )
    return materialize(source, WORK, x.id, x.workspace.files)


def one(a, command):
    if len(a) > 1:
        die(f'Usage: ./rootkitlings {command} [EXERCISE]')
    return a[0] if a else None


def none(a, command):
    if a:
        die(f'Usage: ./rootkitlings {command}')


def run(cmd, timeout=None, capture=False, env=None):
    try:
        return run_process(
            cmd,
            cwd=ROOT,
            timeout=timeout,
            capture=capture,
            env=env,
        )
    except ProcessTimeout as error:
        raise CLIError(str(error)) from error
    except ProcessError as error:
        raise CLIError(str(error)) from error


def cmd_start(a):
    with STORE.locked() as s:
        x = resolve(one(a, 'start'), s)
        available(x, s)
        p = wp(x)
        r = s.exercise(x.id)
        created = not p.exists()
        if not created:
            if p.is_symlink() or not p.is_dir():
                die(f'Unsafe workspace: {p.relative_to(ROOT)}')
            if not r.baseline_hash:
                die(f'Workspace exists without recorded baseline: {p.relative_to(ROOT)}')
        else:
            materialize_workspace(x, s)
            r.baseline_hash, _ = create_snapshot(p, PASSED, x.workspace.files)
            provider = CAT.baseline_provider(x)
            r.baseline_provider = provider.id if provider else ''
            r.workspace_hash = canonical_tree_hash(p, x.workspace.files)
        UI.heading(f'{label(x)}: {x.title}')
        state = 'created' if created else 'already exists'
        UI.success(f'Workspace {state}: {shown(p)}')
        UI.section('Edit')
        for relative in x.workspace.editable:
            UI.bullet(relative)
        UI.section('Next')
        UI.command(f'./rootkitlings check {label(x)}')


def cmd_read(a):
    with STORE.read_locked() as s:
        x = resolve(one(a, 'read'), s)
    p = ROOT / x.resources.readme
    viewer = shutil.which('glow') or shutil.which('bat') or shutil.which('batcat')
    if viewer and Path(viewer).name == 'glow':
        width = UI.terminal_width()
        viewed = run([viewer, '-w', str(width), str(p)])
    elif viewer:
        viewed = run([
            viewer,
            '--style=plain',
            '--paging=never',
            '--language=markdown',
            str(p),
        ])
    else:
        UI.raw(p.read_text())
        viewed = None
    if viewed is not None and viewed.returncode:
        die(f'Could not render {label(x)} lesson text')
    workspace = wp(x)
    if workspace.is_dir() and not workspace.is_symlink():
        UI.section('Next')
        UI.command(f'./rootkitlings check {label(x)}')
    else:
        UI.section('Next')
        UI.command(f'./rootkitlings start {label(x)}')


def cmd_hint(a):
    with STORE.read_locked() as s:
        x = resolve(one(a, 'hint'), s)
    UI.heading(f'Hint for {label(x)}: {x.title}')
    UI.raw((ROOT / x.resources.hint).read_text())
    UI.section('Try again')
    UI.command(f'./rootkitlings check {label(x)}')
    UI.section('Still stuck')
    UI.command(f'./rootkitlings solution {label(x)}')


def cmd_solution(a):
    # Self-study references remain intentionally available out of route order.
    with STORE.read_locked() as s:
        x = resolve(one(a, 'solution'), s)
        completed = set(s.completed)
    output = solution_output(x, completed)
    UI.raw(output)
    with STORE.locked() as s:
        s.exercise(x.id).solution_revealed = True
    UI.section('Try again')
    UI.command(f'./rootkitlings check {label(x)}')
    UI.section('Restore starting files')
    UI.command(f'./rootkitlings restore {label(x)}')


def solution_text(data, relative):
    try:
        return data.decode('utf-8')
    except UnicodeDecodeError as error:
        die(f'Reference solution is not UTF-8 text: {relative}')


def solution_output(x, completed):
    tree = CAT.solution_tree(x)
    provider = (
        CAT.solution_provider(x)
        if hasattr(CAT, 'solution_provider')
        else CAT.baseline_provider(x)
    )
    derived = provider is not None and x.workspace.baseline != 'completion'
    lines = []
    if not derived:
        lines.append(
            f'{UI.style("[!]", "warning")} Complete reference files for '
            f'{label(x)}: {x.title}\n'
        )
        lines.append('Viewing this reference is recorded.\n')
        for relative, solution_file in sorted(tree.items()):
            text = solution_text(solution_file.data, relative)
            lines.append(f'\n--- {relative} ---\n')
            lines.append(text)
            if text and not text.endswith('\n'):
                lines.append('\n')
        return ''.join(lines)

    provider_tree = CAT.solution_tree(provider)
    lines.append(
        f'{UI.style("[!]", "warning")} Reference changes for '
        f'{label(x)}: {x.title}\n'
    )
    lines.append('Viewing this reference is recorded.\n')
    lines.append(f'Compared with {label(provider)}: {provider.title}\n')
    if provider.id not in completed:
        lines.append(
            f'Note: {label(provider)} is not complete; these changes compare '
            'with its reference files, not your workspace.\n'
        )
    changed = False
    for relative, solution_file in sorted(tree.items()):
        before = provider_tree.get(relative)
        if before is not None and before.data == solution_file.data:
            continue
        changed = True
        before_text = '' if before is None else solution_text(before.data, relative)
        after_text = solution_text(solution_file.data, relative)
        diff = list(difflib.unified_diff(
            before_text.splitlines(keepends=True),
            after_text.splitlines(keepends=True),
            fromfile='/dev/null' if before is None else f'a/{relative}',
            tofile=f'b/{relative}',
            lineterm='\n',
        ))
        if not diff and before is None:
            diff = ['--- /dev/null\n', f'+++ b/{relative}\n']
        lines.append('\n')
        lines.extend(diff)
    if not changed:
        die(f'Reference files for {label(x)} introduce no changes')
    return ''.join(lines)


def build_workspace(x):
    need(x)
    build_directory = bd(x)
    if build_directory.is_symlink() or not build_directory.is_dir():
        die(f'Unsafe build directory: {shown(build_directory)}')
    artifact = build_directory / CAT.module_artifact(x)
    if artifact.is_symlink():
        die(f'Expected artifact must not be a symlink: {shown(artifact)}')
    if artifact.exists():
        if not artifact.is_file():
            die(f'Expected artifact path is not a regular file: {shown(artifact)}')
        artifact.unlink()
    kdir = course_kernel_build_dir()
    r = run([
        'make',
        '-C',
        str(ROOT),
        f'KDIR={kdir}',
        f'MODULE_DIR={bd(x)}',
        'W=1',
        '--quiet',
        'module',
    ], env=module_build_environment())
    if r.returncode:
        die(f'Build failed for {label(x)}')
    if artifact.is_symlink() or not artifact.is_file():
        die(f'Build produced no fresh regular artifact: {shown(artifact)}')
    return artifact


def course_kernel_build_dir():
    located = run(
        [str(ROOT / 'scripts/kernel'), 'build-dir'],
        timeout=10,
        capture=True,
    )
    output = (located.stdout or '').strip()
    if located.returncode or not output or '\n' in output:
        die('Course kernel is not ready; run: ./rootkitlings setup')
    path = Path(output)
    if path.is_symlink() or not path.is_dir():
        die('Course kernel build directory is unsafe or missing')
    return path


def run_host_check(x, build_directory, artifact, timeout):
    return run([
        str(ROOT / x.resources.host_check),
        str(build_directory),
        str(artifact),
    ], timeout=timeout, capture=True)


def cmd_build(a):
    with STORE.locked() as s:
        x = resolve(one(a, 'build'), s)
        available(x, s)
        workspace = wp(x)
        record = s.exercise(x.id)
        if workspace.exists():
            if workspace.is_symlink() or not workspace.is_dir():
                die(f'Unsafe workspace: {workspace.relative_to(ROOT)}')
            if not record.baseline_hash:
                die(
                    'Workspace exists without recorded baseline: '
                    f'{workspace.relative_to(ROOT)}'
                )
        else:
            materialize_workspace(x, s)
            record.baseline_hash, _ = create_snapshot(
                workspace,
                PASSED,
                x.workspace.files,
            )
            provider = CAT.baseline_provider(x)
            record.baseline_provider = provider.id if provider else ''
            record.workspace_hash = canonical_tree_hash(
                workspace,
                x.workspace.files,
            )
    module = build_workspace(x)
    UI.success(f'Built {label(x)}: {shown(module)}')


def tools(x):
    out = []
    registry = {tool.id: tool.binary for tool in CAT.tools}
    for tid in x.verification.tools:
        if tid not in registry:
            die(f'Catalog has no verification tool {tid}')
        out += ['--tool', registry[tid]]
    return out


def bootargs(x):
    z = []
    for capability in x.verification.capabilities:
        z += ['--cap', capability]
    return z + tools(x)


def check_timeout(x):
    value = os.getenv('ROOTKITLINGS_CHECK_TIMEOUT')
    if value is None:
        return x.verification.timeout_seconds
    if not value.isdigit() or int(value) <= 0:
        die('ROOTKITLINGS_CHECK_TIMEOUT must be a positive integer')
    return int(value)


def verify_edit_boundary(x, source, starting_snapshot):
    """Reject changes outside the exercise's declared learner edit surface."""
    editable = set(x.workspace.editable)
    frozen = tuple(
        relative
        for relative in x.workspace.files
        if relative not in editable
    )
    if not frozen:
        return
    try:
        if canonical_tree_hash(source, frozen) == canonical_tree_hash(
            starting_snapshot,
            frozen,
        ):
            return
        changed = [
            relative
            for relative in frozen
            if canonical_tree_hash(source, (relative,)) != canonical_tree_hash(
                starting_snapshot,
                (relative,),
            )
        ]
    except WorkspaceError as error:
        raise VerificationError(
            f'cannot verify the learner edit boundary: {error}'
        ) from error
    raise VerificationError(
        'non-editable files changed: ' + ', '.join(changed)
    )


def record_attempt(x, passed, verified_source=None, verified_hash=None):
    changed = False
    with STORE.locked() as s:
        available(x, s)
        r = s.exercise(x.id)
        r.attempts += 1
        try:
            live_hash = canonical_tree_hash(wp(x), x.workspace.files)
        except WorkspaceError:
            live_hash = ''
        r.workspace_hash = live_hash
        if passed:
            if verified_source is None or verified_hash is None:
                raise CLIError('successful attempt requires a verified frozen source')
            if live_hash != verified_hash:
                changed = True
            else:
                content_hash, _ = create_snapshot(
                    verified_source,
                    PASSED,
                    x.workspace.files,
                )
                if content_hash != verified_hash:
                    raise CLIError('verified snapshot hash changed unexpectedly')
                r.passed_snapshot = content_hash
                r.workspace_hash = content_hash
                s.completed.add(x.id)
    return changed


def cmd_check(a):
    with STORE.read_locked() as s:
        x = resolve(one(a, 'check'), s)
        available(x, s)
        record = s.exercises.get(x.id)
        baseline_hash = record.baseline_hash if record else ''
    workspace = need(x)
    if not baseline_hash:
        die(f'{label(x)} has no recorded starting snapshot')
    starting_snapshot = snap(baseline_hash)
    timeout = check_timeout(x)
    try:
        cases = CAT.verification_cases(x)
        with build_verification_cases(
            ROOT,
            workspace,
            x.workspace.files,
            x.workspace.build,
            CAT.module_artifact(x),
            cases,
            timeout,
            kernel_build_dir=course_kernel_build_dir(),
        ) as verification:
            verify_edit_boundary(
                x,
                verification.frozen_source,
                starting_snapshot,
            )
            outcomes = verification.outcomes
            host_check_pending = bool(x.resources.host_check)
            for index, outcome in enumerate(outcomes, 1):
                UI.progress(f'Case {index} of {len(outcomes)}  {outcome.case.label}')
                if outcome.artifact is None:
                    UI.success('Build rejected as required')
                    continue
                if host_check_pending:
                    host = run_host_check(
                        x,
                        outcome.build_root,
                        outcome.artifact,
                        timeout,
                    )
                    if host.stdout:
                        UI.raw_block(host.stdout, error=bool(host.returncode))
                    if host.returncode:
                        die(f'Source and artifact checks failed for {label(x)}')
                    host_check_pending = False
                result = run(
                    [
                        str(ROOT / 'vm/boot'),
                        *bootargs(x),
                        '--check',
                        str(ROOT / outcome.case.check),
                        *(
                            value
                            for argument in getattr(
                                outcome.case, 'check_arguments', ()
                            )
                            for value in ('--check-arg', argument)
                        ),
                        str(outcome.artifact),
                    ],
                    timeout=timeout,
                    capture=True,
                )
                if result.stdout:
                    UI.raw_block(result.stdout, error=bool(result.returncode))
                if result.returncode:
                    die(f'Guest check failed: {outcome.case.label}')
            if host_check_pending:
                die(f'No successful artifact was available for {label(x)} host checks')
            verified_source = verification.frozen_source
            verified_hash = verification.source_hash
            workspace_changed = record_attempt(
                x,
                True,
                verified_source,
                verified_hash,
            )
    except (CLIError, VerificationError) as error:
        record_attempt(x, False)
        raise CLIError(
            str(error),
            recovery=(('Try', f'./rootkitlings hint {label(x)}'),),
        ) from error
    if workspace_changed:
        die(
            'Workspace changed while checks were running; no pass was recorded. '
            f'Rerun: ./rootkitlings check {label(x)}'
        )
    UI.success(f'Passed {label(x)}')
    with STORE.read_locked() as s:
        next_item = current(s)
        if next_item is None:
            UI.success('Course complete')
        else:
            show_next(s, kernel_ready=True)
            UI.line(next_item.title, style='dim')


def cmd_run(a, gdb=False):
    command_name = 'gdb' if gdb else 'run'
    q = None
    extra = []
    i = 0
    while i < len(a):
        if a[i] == '--cap':
            if i + 1 == len(a):
                die('--cap requires a capability')
            extra += ['--cap', a[i + 1]]
            i += 2
        elif a[i].startswith('-'):
            die(f'Unknown option: {a[i]}')
        elif q is None:
            q = a[i]
            i += 1
        else:
            die(f'{command_name} accepts at most one exercise')
    with STORE.read_locked() as s:
        x = resolve(q, s)
        available(x, s)
    command = [
        str(ROOT / 'vm/boot'),
        *(['--gdb'] if gdb else []),
        *bootargs(x),
        *extra,
        str(build_workspace(x)),
    ]
    raise SystemExit(run(command).returncode)


def cmd_status(a):
    none(a, 'status')
    kernel_ready = course_kernel_ready()
    UI.heading('Rootkitlings')
    with STORE.read_locked() as s:
        x = current(s)
        completed = len(set(ROUTE) & s.completed)
        UI.field('Course', f'{completed} of {len(ROUTE)} complete')
        UI.field(
            'Kernel',
            'Ready' if kernel_ready else 'Setup required',
            value_style='success' if kernel_ready else 'warning',
        )
        if not x:
            UI.section('Course complete')
            return
        UI.section(f'Current {kind_label(x).lower()}')
        UI.field(label(x), x.title, value_style='accent')
        UI.field('Stage', CAT.section(x))
        UI.field('Level', x.difficulty.capitalize())
        workspace = wp(x)
        if workspace.is_dir() and not workspace.is_symlink():
            UI.field('Workspace', shown(workspace))
            UI.section('Files to edit')
            for relative in x.workspace.editable:
                UI.bullet(relative)
        else:
            UI.field('Workspace', 'Not started', value_style='warning')
        show_next(s, kernel_ready=kernel_ready)


def cmd_list(a):
    none(a, 'list')
    kernel_ready = course_kernel_ready()
    with STORE.read_locked() as s:
        completed = len(set(ROUTE) & s.completed)
        UI.heading('Course route')
        UI.field('Course', f'{completed} of {len(ROUTE)} complete')
        stage = None
        for x in CAT.required():
            if x.stage != stage:
                UI.section(CAT.section(x))
                stage = x.stage
            if x.id in s.completed:
                state = 'passed'
                style = 'success'
            elif x.id == s.current:
                state = 'current'
                style = 'accent'
            else:
                state = 'locked'
                style = 'dim'
            UI.line(
                f'  {state:<8} {label(x):<4} {x.title} '
                f'({x.difficulty.capitalize()})',
                style=style,
            )
        if CAT.elective_tracks:
            UI.section('Optional tracks')
        for track in CAT.elective_tracks:
            UI.heading(f'{track.serial}  {track.label}')
            for exercise_id in track.members:
                x = CAT.by_id[exercise_id]
                if x.id in s.completed:
                    state = 'passed'
                    style = 'success'
                elif all(
                    prerequisite in s.completed
                    for prerequisite in x.prerequisites
                ):
                    state = 'optional'
                    style = 'accent'
                else:
                    state = 'locked'
                    style = 'dim'
                UI.line(
                    f'  {state:<8} {label(x):<4} '
                    f'{x.title} ({x.difficulty.capitalize()})',
                    style=style,
                )
        if s.current:
            show_next(s, kernel_ready=kernel_ready)


def cmd_prereq(a):
    with STORE.read_locked() as s:
        x = resolve(one(a, 'prereq'), s)
        completed = set(s.completed)
    UI.heading(f'Prerequisites for {label(x)}: {x.title}')
    UI.section('Exercises')
    if not x.prerequisites:
        UI.bullet('None')
    for p in x.prerequisites:
        y = CAT.by_id[p]
        state = 'complete' if p in completed else 'required'
        UI.bullet(f'{label(y)}  {y.title} ({state})')
    if x.requires:
        UI.section('Concepts')
        for c in x.requires:
            assumption = CAT.assumption(c)
            detail = f': {assumption.competency}' if assumption else ''
            UI.bullet(f'{CAT.concept_label(c)}{detail}')
    UI.section('Next')
    UI.command(f'./rootkitlings read {label(x)}')


def cmd_restore(a):
    with STORE.locked() as s:
        x = resolve(one(a, 'restore'), s)
        need(x)
        r = s.exercises.get(x.id)
        if not r or not r.baseline_hash:
            die(f'{label(x)} has no recorded baseline')
        restored, arch = restore_workspace(
            WORK,
            ARCHIVE,
            x.id,
            snap(r.baseline_hash),
            x.workspace.files,
        )
        r.workspace_hash = canonical_tree_hash(restored, x.workspace.files)
    UI.success(f'Edits archived: {shown(arch)}')
    UI.success(f'Starting files restored for {label(x)}; progress unchanged')
    UI.section('Next')
    UI.line('Edit: ' + ', '.join(x.workspace.editable))
    UI.command(f'./rootkitlings check {label(x)}')


def progress_reset_ids(selected):
    if selected is None:
        affected = set(CAT.by_id)
    elif selected.id in ROUTE:
        affected = set(ROUTE[ROUTE.index(selected.id):])
    else:
        affected = {selected.id}

    changed = True
    while changed:
        changed = False
        for exercise in CAT.exercises:
            if exercise.status == 'required' or exercise.id in affected:
                continue
            dependencies = set(CAT.completion_dependencies(exercise))
            if dependencies & affected:
                affected.add(exercise.id)
                changed = True
    return affected


def cmd_reset(a):
    reset_all = a == ['--all', '--yes']
    if a == ['--all']:
        die('Resetting all progress requires: ./rootkitlings reset --all --yes')
    if not reset_all and len(a) != 1:
        die('Usage: ./rootkitlings reset EXERCISE | reset --all --yes')
    if not reset_all and a[0].startswith('-'):
        die('Usage: ./rootkitlings reset EXERCISE | reset --all --yes')
    with STORE.locked() as s:
        selected = None if reset_all else resolve(a[0], s)
        affected = progress_reset_ids(selected)
        archives = archive_and_remove_workspaces(WORK, ARCHIVE, affected)
        s.clear_progress(affected)
    if archives:
        UI.success(f'Archived {len(archives)} workspace(s) under {shown(ARCHIVE)}')
    scope = 'all course progress' if reset_all else f'progress from {label(selected)} onward'
    UI.success(f'Reset {scope}')


def cmd_clean(a):
    with STORE.read_locked() as s:
        x = resolve(one(a, 'clean'), s)
    need(x)
    k = course_kernel_build_dir()
    if run(['make', '-C', str(k), f'M={bd(x)}', 'clean']).returncode:
        die(f'Could not clean {label(x)}')
    UI.success(f'Cleaned {label(x)} build outputs')


def usage():
    sections = (
        ('Setup', (
            ('setup', 'Prepare the kernel and verify the disposable guest'),
        )),
        ('Learn', (
            ('read [EXERCISE]', 'Read the lesson'),
            ('prereq [EXERCISE]', 'Show direct prerequisites'),
            ('hint [EXERCISE]', 'Get a focused nudge'),
            ('solution [EXERCISE]', 'Show the reference changes'),
        )),
        ('Work', (
            ('start [EXERCISE]', 'Create or reopen the workspace'),
            ('build [EXERCISE]', 'Compile without running checks'),
            ('check [EXERCISE]', 'Build and verify in a disposable guest'),
            ('run [EXERCISE]', 'Open an interactive disposable guest'),
            ('gdb [EXERCISE]', 'Open a guest waiting for GDB'),
        )),
        ('Progress', (
            ('status', 'Show progress and the next action'),
            ('list', 'Show the complete course route'),
            ('restore [EXERCISE]', 'Archive edits and restore starting files'),
            ('reset EXERCISE', 'Reset this exercise and everything after it'),
            ('reset --all --yes', 'Reset all course progress'),
            ('clean [EXERCISE]', 'Remove build outputs'),
            ('help', 'Show this help'),
        )),
    )
    UI.heading('Rootkitlings')
    UI.line('Learn kernel development by building one rootkit mechanism at a time.')
    UI.section('Usage')
    UI.command('./rootkitlings <COMMAND> [EXERCISE]')
    for title, commands in sections:
        UI.section(title)
        for command, description in commands:
            UI.row(command, description, left_style='command')
    UI.section('Exercise selectors')
    UI.line('Use a serial such as L00, a stable ID, or an unambiguous prefix.')
    UI.line('If EXERCISE is omitted, the current exercise is used.')
    UI.line('Commands also accept unambiguous prefixes.')


CMDS = {
    'setup': cmd_setup,
    'read': cmd_read,
    'start': cmd_start,
    'build': cmd_build,
    'check': cmd_check,
    'hint': cmd_hint,
    'solution': cmd_solution,
    'run': cmd_run,
    'gdb': lambda a: cmd_run(a, True),
    'status': cmd_status,
    'list': cmd_list,
    'prereq': cmd_prereq,
    'restore': cmd_restore,
    'reset': cmd_reset,
    'clean': cmd_clean,
    'help': lambda a: (none(a, 'help'), usage()),
}


def main(a):
    requested = a[0] if a else 'status'
    arguments = a[1:] if a else []
    if requested in {'-h', '--help'}:
        none(arguments, requested)
        usage()
        return 0
    matches = [command for command in CMDS if command.startswith(requested)]
    if not matches:
        die(f'Unknown command: {requested}')
    if len(matches) > 1:
        die(f'Ambiguous command {requested!r}. Matches: {", ".join(matches)}')
    command = matches[0]
    if command == 'help':
        CMDS[command](arguments)
        return 0
    initialize_runtime()
    if command in {'start', 'build', 'check', 'run', 'gdb'}:
        require_setup()
    CMDS[command](arguments)
    return 0


def entrypoint():
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (
        CLIError,
        StateError,
        VerificationError,
        WorkspaceError,
    ) as error:
        UI.error(str(error))
        for title, command in getattr(error, 'recovery', ()):
            UI.section(title, error=True)
            UI.command(command, error=True)
        raise SystemExit(1)


if __name__ == '__main__':
    entrypoint()
