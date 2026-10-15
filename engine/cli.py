#!/usr/bin/env python3
"""Rklings learner CLI."""

from __future__ import annotations

import os
import shutil
import sys
import tempfile
from pathlib import Path

from engine.catalog import Catalog, CatalogError
from engine.build_env import module_build_environment
from engine.console import Console
from engine.process import ProcessError, ProcessTimeout, run_process
from engine.control_console import (
    OperatorError,
    create_target_session,
    read_target_session,
    run_control_console,
    wait_for_operator,
)
from engine.server import run_server, server_running
from engine.state import StateStore, StateError
from engine.verification import VerificationError, build_verification_cases
from engine.workspace import (
    WorkspaceError,
    archive_and_remove_workspaces,
    canonical_tree_hash,
    clean_paths,
    create_snapshot,
    materialize,
    restore_workspace,
    verify_snapshot,
)

ROOT = Path(__file__).resolve().parent.parent
RT = ROOT / '.rklings'
STATE = RT / 'state.toml'
WORK = ROOT / 'work'
BUILD = RT / 'build'
SNAPSHOTS = RT / 'snapshots'
BACKUPS = RT / 'backups'
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
STORE = StateStore(STATE, ROUTE, CAT.revision, COMPLETION_DEPENDENCIES)


def initialize_runtime():
    for path in (RT, WORK, BUILD, SNAPSHOTS, BACKUPS):
        if path.is_symlink():
            relative = path.relative_to(ROOT)
            die(f'Runtime directory must not be a symlink: {relative}')
        try:
            path.mkdir(parents=True, exist_ok=True)
        except OSError as error:
            relative = path.relative_to(ROOT)
            die(f'Cannot create runtime directory {relative}: {error}')
        if not path.is_dir():
            relative = path.relative_to(ROOT)
            die(f'Runtime path must be a directory: {relative}')


def course_kernel_ready():
    kernel = run(
        [str(ROOT / 'scripts/kernel'), 'status'],
        timeout=10,
        capture=True,
    )
    return kernel.returncode == 0


def require_setup():
    if not course_kernel_ready():
        die('Course kernel is not ready. Run: ./rklings setup')


def cmd_setup(a):
    none(a, 'setup')
    UI.heading('Prepare course VM')
    UI.progress('Checking host tools')
    doctor = run(
        [str(ROOT / 'scripts/doctor')],
        timeout=DOCTOR_TIMEOUT,
        capture=True,
        visible=True,
    )
    if doctor.returncode:
        die('Setup failed: scripts/doctor did not pass')
    UI.success('Host tools ready')
    UI.progress(
        'Preparing the pinned course kernel; the first build may be quiet '
        'for several minutes'
    )
    kernel = run(
        [str(ROOT / 'scripts/kernel'), 'ensure'],
        timeout=KERNEL_PREP_TIMEOUT,
        visible=True,
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
        visible=True,
    )
    if containment.returncode:
        die('Setup failed: disposable guest containment check did not pass')
    UI.success('Guest boundary verified')
    UI.success('Setup ready')
    with STORE.read_locked() as state:
        show_next(state, kernel_ready=True)


def label(item):
    return item.serial or item.id


def workspace_name(item):
    """Return the short learner-facing directory name for an exercise."""
    return item.serial or item.id


def kind_label(item):
    return {
        'lesson': 'Lesson',
        'milestone': 'Milestone',
        'final-project': 'Final project',
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
        return './rklings setup'
    workspace = wp(item)
    if workspace.is_dir() and not workspace.is_symlink():
        return f'./rklings check {label(item)}'
    return f'./rklings read {label(item)}'


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
    if x.id in state.completed or state.current is None:
        return
    if ROUTE.index(x.id) > ROUTE.index(state.current):
        now = CAT.by_id[state.current]
        die(
            f'{label(x)} is locked. Complete {label(now)}: '
            f'{now.title} first.'
        )


def wp(x):
    return WORK / workspace_name(x)


def shown_editable(x):
    return tuple(shown(wp(x) / relative) for relative in x.workspace.editable)


def bd(x):
    return wp(x) if x.workspace.build == '.' else wp(x) / x.workspace.build


def build_output(x):
    return BUILD / workspace_name(x)


def need(x):
    p = wp(x)
    if not p.is_dir() or p.is_symlink():
        die(
            f'{label(x)} has no workspace. '
            f'Run: ./rklings start {label(x)}'
        )
    return p


def snap(content_hash):
    verify_snapshot(SNAPSHOTS / content_hash, content_hash)
    return SNAPSHOTS / content_hash


def materialize_workspace(x, state):
    source = ROOT / x.workspace.source
    return materialize(source, WORK, workspace_name(x), x.workspace.files)


def one(a, command):
    if len(a) > 1:
        die(f'Usage: ./rklings {command} [EXERCISE]')
    return a[0] if a else None


def none(a, command):
    if a:
        die(f'Usage: ./rklings {command}')


def run(cmd, timeout=None, capture=False, env=None, visible=False):
    if visible:
        UI.transcript(cmd)
    try:
        result = run_process(
            cmd,
            cwd=ROOT,
            timeout=timeout,
            capture=capture,
            env=env,
        )
        if visible and capture:
            UI.process_output(
                result.stdout or '',
                error=bool(result.returncode),
            )
        if visible:
            UI.process_result(result.returncode)
        return result
    except ProcessTimeout as error:
        if visible and capture:
            UI.process_output(error.output, error=True)
        if visible:
            UI.process_result(None)
            message = f'Command timed out after {error.timeout} seconds'
            raise CLIError(message) from error
        raise CLIError(str(error)) from error
    except ProcessError as error:
        if visible and capture:
            UI.process_output('', error=True)
        if visible:
            UI.process_result(None)
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
                relative = p.relative_to(ROOT)
                die(f'Workspace exists without recorded baseline: {relative}')
        else:
            materialize_workspace(x, s)
            r.baseline_hash, _ = create_snapshot(
                p,
                SNAPSHOTS,
                x.workspace.files,
            )
            r.workspace_hash = canonical_tree_hash(p, x.workspace.files)
        UI.heading(f'{label(x)}: {x.title}')
        state = 'created' if created else 'already exists'
        UI.success(f'Workspace {state}: {shown(p)}')
        UI.section('Edit')
        for path in shown_editable(x):
            UI.bullet(path)
        UI.section('Next')
        UI.command(f'./rklings check {label(x)}')


def cmd_read(a):
    with STORE.read_locked() as s:
        x = resolve(one(a, 'read'), s)
    p = ROOT / x.resources.readme
    viewer = shutil.which('batcat') or shutil.which('bat')
    if viewer:
        command = [
            viewer,
            '--style=plain',
            '--paging=never',
            '--color=always' if UI.color_enabled() else '--color=never',
            f'--terminal-width={UI.terminal_width()}',
            '--language=markdown',
            str(p),
        ]
        rendered = run(command, capture=True)
        if rendered.returncode == 0:
            UI.raw(rendered.stdout or '')
        else:
            UI.warning('Syntax highlighting failed; showing plain Markdown')
            UI.transcript(command)
            UI.process_output(rendered.stdout or '', error=True)
            UI.process_result(rendered.returncode)
            UI.raw(p.read_text())
    else:
        UI.raw(p.read_text())
    workspace = wp(x)
    if workspace.is_dir() and not workspace.is_symlink():
        UI.section('Next')
        UI.command(f'./rklings check {label(x)}')
    else:
        UI.section('Next')
        UI.command(f'./rklings start {label(x)}')


def cmd_hint(a):
    with STORE.read_locked() as s:
        x = resolve(one(a, 'hint'), s)
    UI.heading(f'Hint for {label(x)}: {x.title}')
    UI.raw((ROOT / x.resources.hint).read_text())
    workspace = wp(x)
    workspace_ready = workspace.is_dir() and not workspace.is_symlink()
    UI.section('Try again' if workspace_ready else 'Next')
    command = 'check' if workspace_ready else 'start'
    UI.command(f'./rklings {command} {label(x)}')
    UI.section('Still stuck')
    UI.command(f'./rklings solution {label(x)}')


def cmd_solution(a):
    # Self-study references remain intentionally available out of route order.
    with STORE.read_locked() as s:
        x = resolve(one(a, 'solution'), s)
    output = solution_output(x)
    UI.raw(output)
    workspace = wp(x)
    if workspace.is_dir() and not workspace.is_symlink():
        UI.section('Try again')
        UI.command(f'./rklings check {label(x)}')
        UI.section('Restore starting files')
        UI.command(f'./rklings restore {label(x)}')
    else:
        UI.section('Next')
        UI.command(f'./rklings start {label(x)}')


def solution_text(data, relative):
    try:
        return data.decode('utf-8')
    except UnicodeDecodeError as error:
        die(f'Reference solution is not UTF-8 text: {relative}')


def solution_output(x):
    tree = CAT.solution_tree(x)
    lines = [f'Complete reference files for {label(x)}: {x.title}\n']
    for relative, solution_file in sorted(tree.items()):
        text = solution_text(solution_file.data, relative)
        lines.append(f'\n--- {relative} ---\n')
        lines.append(text)
        if text and not text.endswith('\n'):
            lines.append('\n')
    return ''.join(lines)


def build_source(x, source_directory, output_directory):
    if source_directory.is_symlink() or not source_directory.is_dir():
        die(f'Unsafe source directory: {shown(source_directory)}')
    if output_directory.is_symlink():
        die(f'Build output must not be a symlink: {shown(output_directory)}')
    try:
        output_directory.mkdir(parents=True, exist_ok=True)
    except OSError as error:
        die(f'Cannot create build output {shown(output_directory)}: {error}')
    if not output_directory.is_dir():
        die(f'Build output must be a directory: {shown(output_directory)}')
    artifact = output_directory / CAT.module_artifact(x)
    if artifact.is_symlink():
        die(f'Expected artifact must not be a symlink: {shown(artifact)}')
    if artifact.exists():
        if not artifact.is_file():
            die(
                'Expected artifact path is not a regular file: '
                f'{shown(artifact)}'
            )
        artifact.unlink()
    kdir = course_kernel_build_dir()
    r = run([
        'make',
        '-C',
        str(ROOT),
        f'KDIR={kdir}',
        f'MODULE_DIR={source_directory}',
        f'MODULE_OUT={output_directory}',
        'W=1',
        '--quiet',
        'module',
    ], capture=True, env=module_build_environment(), visible=True)
    if r.returncode:
        die(f'Build failed for {label(x)}')
    if artifact.is_symlink() or not artifact.is_file():
        die(f'Build produced no fresh regular artifact: {shown(artifact)}')
    return artifact


def build_workspace(x):
    need(x)
    return build_source(x, bd(x), build_output(x))


def course_kernel_build_dir():
    located = run(
        [str(ROOT / 'scripts/kernel'), 'build-dir'],
        timeout=10,
        capture=True,
    )
    output = (located.stdout or '').strip()
    if located.returncode or not output or '\n' in output:
        die('Course kernel is not ready; run: ./rklings setup')
    path = Path(output)
    if path.is_symlink() or not path.is_dir():
        die('Course kernel build directory is unsafe or missing')
    return path


def run_host_check(x, build_directory, artifact, timeout):
    return run([
        str(ROOT / x.resources.host_check),
        str(build_directory),
        str(artifact),
    ], timeout=timeout, capture=True, visible=True)


def cmd_build(a):
    with STORE.read_locked() as s:
        x = resolve(one(a, 'build'), s)
        available(x, s)
        record = s.exercises.get(x.id)
        workspace = need(x)
        if not record or not record.baseline_hash:
            die(f'{label(x)} has no recorded starting snapshot')
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
    return tools(x)


def boot_flags(x):
    return [*(['--bare'] if x.stage == 'module-core' else []), *bootargs(x)]


def boot_exercise(x, module):
    if x.runtime is not None:
        try:
            target = create_target_session(ROOT, label(x))
        except OperatorError as error:
            raise CLIError(str(error)) from error
        try:
            if server_running(ROOT):
                UI.progress('Server detected, waiting for port binding')
            else:
                UI.section('Start the operator server first')
                UI.command('./rklings server')
                UI.progress('Waiting for operator')
            wait_for_operator(target)
            UI.success('Operator ready')
            UI.section('Target console')
            command = [
                str(ROOT / 'vm/boot'),
                '--runtime-setup', str(ROOT / x.runtime.setup),
                '--control-key-file', str(target.key_file),
                '--control-port', str(target.control_port),
                '--shell-port', str(target.shell_port),
                str(module),
            ]
            return run(command, visible=True).returncode
        finally:
            target.close()
    command = [
        str(ROOT / 'vm/boot'),
        *boot_flags(x),
        str(module),
    ]
    return run(command, visible=True).returncode


def check_timeout(x):
    value = os.getenv('RKLINGS_CHECK_TIMEOUT')
    if value is None:
        return x.verification.timeout_seconds
    if not value.isdigit() or int(value) <= 0:
        die('RKLINGS_CHECK_TIMEOUT must be a positive integer')
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
                raise CLIError(
                    'successful attempt requires a verified frozen source'
                )
            if live_hash != verified_hash:
                changed = True
            else:
                content_hash, _ = create_snapshot(
                    verified_source,
                    SNAPSHOTS,
                    x.workspace.files,
                )
                if content_hash != verified_hash:
                    raise CLIError(
                        'verified snapshot hash changed unexpectedly'
                    )
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
    cases = x.verification.cases
    UI.heading(f'Check {label(x)}: {x.title}')
    UI.field('Cases', str(len(cases)))
    UI.field('Timeout', f'{timeout} seconds')
    UI.section('Build cases')

    def before_build(index, total, case, command):
        UI.heading(f'{index}/{total} {case.label}')
        expected = (
            'module artifact'
            if case.expect_build == 'pass'
            else 'rejected build'
        )
        UI.field('Expected', expected)
        UI.transcript(command)

    def after_build(case, output, returncode):
        unexpected_failure = (
            returncode not in (0, None)
            and case.expect_build == 'pass'
        )
        UI.process_output(output, error=unexpected_failure)
        UI.process_result(
            returncode,
            expected_nonzero=case.expect_build == 'fail',
        )

    try:
        with build_verification_cases(
            ROOT,
            workspace,
            x.workspace.files,
            x.workspace.build,
            CAT.module_artifact(x),
            cases,
            timeout,
            kernel_build_dir=course_kernel_build_dir(),
            before_build=before_build,
            after_build=after_build,
        ) as verification:
            verify_edit_boundary(
                x,
                verification.frozen_source,
                starting_snapshot,
            )
            outcomes = verification.outcomes
            host_check_pending = bool(x.resources.host_check)
            UI.section('Behavior checks')
            for index, outcome in enumerate(outcomes, 1):
                UI.heading(f'{index}/{len(outcomes)} {outcome.case.label}')
                if outcome.artifact is None:
                    UI.success('Build rejected as required')
                    continue
                UI.field('Artifact', shown(outcome.artifact))
                if host_check_pending:
                    UI.heading('Source and artifact')
                    host = run_host_check(
                        x,
                        outcome.build_root,
                        outcome.artifact,
                        timeout,
                    )
                    if host.returncode:
                        die(f'Source and artifact checks failed for {label(x)}')
                    host_check_pending = False
                UI.heading('Guest behavior')
                result = run(
                    [
                        str(ROOT / 'vm/boot'),
                        *bootargs(x),
                        '--check',
                        str(ROOT / outcome.case.check),
                        *(
                            value
                            for argument in outcome.case.check_arguments
                            for value in ('--check-arg', argument)
                        ),
                        str(outcome.artifact),
                    ],
                    timeout=timeout,
                    capture=True,
                    visible=True,
                )
                if result.returncode:
                    die(f'Guest check failed: {outcome.case.label}')
            if host_check_pending:
                die(
                    f'No successful artifact was available for {label(x)} '
                    'host checks'
                )
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
            recovery=(('Try', f'./rklings hint {label(x)}'),),
        ) from error
    if workspace_changed:
        die(
            'Workspace changed while checks were running; '
            'no pass was recorded. '
            f'Rerun: ./rklings check {label(x)}'
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
    reference = False
    i = 0
    while i < len(a):
        if a[i] == '--cap':
            if i + 1 == len(a):
                die('--cap requires a capability')
            extra += ['--cap', a[i + 1]]
            i += 2
        elif a[i] == '--reference':
            if reference:
                die('--reference may be specified only once')
            reference = True
            i += 1
        elif a[i].startswith('-'):
            die(f'Unknown option: {a[i]}')
        elif q is None:
            q = a[i]
            i += 1
        else:
            die(f'{command_name} accepts at most one exercise')
    with STORE.read_locked() as s:
        x = resolve(q, s)
        if not reference:
            available(x, s)
    if reference:
        if gdb:
            die('--reference cannot be combined with gdb')
        if extra:
            die('--reference cannot be combined with --cap')
        UI.heading(f'Target {label(x)}: {x.title}')
        UI.field('Source', 'Completed reference')
        UI.field('Guest', 'Restricted QEMU')
        with tempfile.TemporaryDirectory(
            prefix='rklings-reference-'
        ) as directory:
            root = Path(directory)
            source = CAT.materialize_solution(x, root, 'source')
            module_source = (
                source
                if x.workspace.build == '.'
                else source / x.workspace.build
            )
            module = build_source(x, module_source, root / 'build')
            status = boot_exercise(x, module)
        if status:
            die(f'Target exited with status {status}')
        UI.success(f'Target {label(x)} stopped')
        return
    module = build_workspace(x)
    command = [
        str(ROOT / 'vm/boot'),
        *(['--gdb'] if gdb else []),
        *boot_flags(x),
        *extra,
        str(module),
    ]
    raise SystemExit(run(command, visible=True).returncode)


def cmd_server(a):
    none(a, 'server')
    try:
        status = run_server(
            ROOT,
            transcript=UI.transcript,
        )
    except OperatorError as error:
        raise CLIError(str(error)) from error
    if status:
        die(f'Server exited with status {status}')


def cmd_connect(a):
    with STORE.read_locked() as state:
        item = resolve(one(a, 'connect'), state)
    if item.runtime is None:
        die(f'{label(item)} has no interactive completed reference')
    try:
        target = read_target_session(ROOT, label(item))
        UI.heading(f'{label(item)} control console')
        UI.field('Target', item.title)
        UI.field('Control', 'AES-256-GCM authenticated')
        UI.field('Shell', 'Ticket-bound plaintext PTY')
        status = run_control_console(
            ROOT,
            target,
            transcript=UI.transcript,
        )
    except OperatorError as error:
        raise CLIError(
            str(error),
            recovery=((
                'Start the target in another terminal',
                f'./rklings run {label(item)} --reference',
            ),),
        ) from error
    if status:
        die(f'Control console exited with status {status}')
    UI.success('Console disconnected; the target is still running')


def cmd_status(a):
    none(a, 'status')
    kernel_ready = course_kernel_ready()
    UI.heading('Rklings')
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
            record = s.exercises.get(x.id)
            try:
                live_hash = canonical_tree_hash(
                    workspace,
                    x.workspace.files,
                )
            except WorkspaceError:
                workspace_state = 'Invalid'
                state_style = 'error'
            else:
                if record and record.passed_snapshot == live_hash:
                    workspace_state = 'Passed'
                    state_style = 'success'
                elif record and record.baseline_hash == live_hash:
                    workspace_state = 'Unchanged'
                    state_style = 'dim'
                else:
                    workspace_state = 'Modified'
                    state_style = 'warning'
            UI.field('State', workspace_state, value_style=state_style)
            if record and record.attempts:
                UI.field('Attempts', str(record.attempts))
            UI.section('Files to edit')
            for path in shown_editable(x):
                UI.bullet(path)
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
        state = ' (complete)' if p in completed else ''
        UI.bullet(f'{label(y)}  {y.title}{state}')
    if x.requires:
        UI.section('Concepts')
        for c in x.requires:
            assumption = CAT.assumption(c)
            detail = f': {assumption.competency}' if assumption else ''
            UI.bullet(f'{CAT.concept_label(c)}{detail}')
    UI.section('Next')
    UI.command(f'./rklings read {label(x)}')


def cmd_restore(a):
    with STORE.locked() as s:
        x = resolve(one(a, 'restore'), s)
        need(x)
        r = s.exercises.get(x.id)
        if not r or not r.baseline_hash:
            die(f'{label(x)} has no recorded baseline')
        restored, arch = restore_workspace(
            WORK,
            BACKUPS,
            workspace_name(x),
            snap(r.baseline_hash),
            x.workspace.files,
        )
        r.workspace_hash = canonical_tree_hash(restored, x.workspace.files)
    UI.success(f'Edits archived: {shown(arch)}')
    UI.success(f'Starting files restored for {label(x)}; progress unchanged')
    UI.section('Edit')
    for path in shown_editable(x):
        UI.bullet(path)
    UI.section('Next')
    UI.command(f'./rklings check {label(x)}')


def progress_reset_ids(selected):
    if selected is None:
        return set(ROUTE)
    return set(ROUTE[ROUTE.index(selected.id):])


def cmd_reset(a):
    reset_all = a == ['--all', '--yes']
    if a == ['--all']:
        die('Resetting all progress requires: ./rklings reset --all --yes')
    if not reset_all and len(a) != 1:
        die('Usage: ./rklings reset EXERCISE | reset --all --yes')
    if not reset_all and a[0].startswith('-'):
        die('Usage: ./rklings reset EXERCISE | reset --all --yes')
    with STORE.locked() as s:
        selected = None if reset_all else resolve(a[0], s)
        affected = progress_reset_ids(selected)
        workspace_names = (
            workspace_name(CAT.by_id[stable_id])
            for stable_id in affected
            if stable_id in s.exercises
            and s.exercises[stable_id].baseline_hash
        )
        archives = archive_and_remove_workspaces(
            WORK,
            BACKUPS,
            workspace_names,
        )
        s.clear_progress(affected)
    if archives:
        UI.success(
            f'Archived {len(archives)} workspace(s) under {shown(BACKUPS)}'
        )
    scope = (
        'all course progress'
        if reset_all
        else f'progress from {label(selected)} onward'
    )
    UI.success(f'Reset {scope}')


def cmd_clean(a):
    with STORE.read_locked() as s:
        x = resolve(one(a, 'clean'), s)
    need(x)
    clean_paths(BUILD, (workspace_name(x),))
    UI.success(f'Cleaned build outputs for {label(x)}')
    UI.field('Sources', shown(wp(x)))
    UI.section('Next')
    UI.command(f'./rklings build {label(x)}')


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
            ('run [EXERCISE]', 'Run your module in a disposable guest'),
            ('run ID --reference', 'Run the completed reference'),
            ('server', 'Start the multi-target operator server'),
            ('connect [EXERCISE]', 'Connect to its control console'),
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
    UI.heading('Rklings')
    UI.line(
        'Learn kernel development by building one rootkit mechanism at a time.'
    )
    UI.section('Usage')
    UI.command('./rklings <COMMAND> [EXERCISE]')
    for title, commands in sections:
        UI.section(title)
        for command, description in commands:
            UI.row(command, description, left_style='command')
    UI.section('Exercise selectors')
    UI.line('Use a serial such as L00, a stable ID, or a unique prefix.')
    UI.line('If EXERCISE is omitted, the current exercise is used.')


CMDS = {
    'setup': cmd_setup,
    'read': cmd_read,
    'start': cmd_start,
    'build': cmd_build,
    'check': cmd_check,
    'hint': cmd_hint,
    'solution': cmd_solution,
    'run': cmd_run,
    'server': cmd_server,
    'connect': cmd_connect,
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
    if requested in {'-V', '--version'}:
        none(arguments, requested)
        UI.line(f'rklings curriculum {CAT.revision}')
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
    if command in {'build', 'check', 'run', 'gdb', 'connect', 'server'}:
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
