# Rootkitlings Repository Guide

Agent instructions for working in this repository.

## Sources of truth

When documents disagree, use this order:

1. `course.toml` defines the curriculum route, concepts, and shared tools.
2. An exercise's `.rklings/exercise.toml` defines its workspace, prerequisites,
   editable files, supplied machinery, runtime, and validation cases.
3. Starter files and `.rklings/solution/` define the implementation delta.
4. `.rklings/checks/` defines what validation actually proves.
5. The exercise README explains the synchronized learner path.

## Architecture

Keep the three operational responsibilities separate:

```text
kernel module (C) <-> local mkdir command boundary <-> agent (Go)
                                                        |
                                                        v
                                                 operator (Go/web)
```

- The kernel module owns privilege, persistence, interception, and concealment.
- The agent owns network communication and translates remote commands into local
  mkdir operations.
- The operator owns authenticated control, terminal workflows, and the web UI.

Do not move networking into the kernel module.
Do not make the operator depend on direct target-kernel access.

## Curriculum

Always read `required_route` in `course.toml` before changing lesson order.
An exercise introduces one bounded capability.
Its `Next` section must name the real limitation resolved by the next entry.

## Exercise layout

Every exercise keeps private machinery below `.rklings/`:

```text
.rklings/exercise.toml  executable lesson contract
.rklings/solution/      reference learner files
.rklings/checks/        behavioral validation cases
.rklings/fixtures/      invalid or supporting inputs, when needed
.rklings/assets/        operational assets, when needed
```

## Manifest discipline

For every exercise:

- `prerequisites` names earlier route entries that establish the baseline.
- `requires` uses concept IDs taught earlier on that path.
- `teaches` contains only concepts actively practiced here.
- `workspace.files` lists every staged learner file.
- `workspace.editable` lists only student-owned files.
- Every verification case names an existing executable check.

## Kernel accuracy

Kernel APIs, symbols, calling conventions, and return contracts must match the
pinned kernel used by the exercise.

When an exercise intercepts a function:

1. Establish whether the target is a syscall wrapper or a regular function.
2. Verify where its arguments are available.
3. Preserve per-instance state needed by the return handler.
4. Define the original behavior for non-matching input.
5. Prove registration rollback and teardown order.

## Validation

Validation proves observable behavior through the real public interface.
Compilation alone is not enough for runtime contracts.

Pair concealment with a control that proves the hidden object still exists.
Do not use log text as the sole proof when the system exposes a stronger
observable result.

Before finishing work, run checks proportionate to the change:

```console
$ make check
$ git diff --check
```

## Code rules

- Keep kernel C small, explicit, and ownership-correct.
- Use real kernel and Go APIs, not teaching-only fake interfaces.
- Supply unrelated plumbing instead of turning it into ceremonial student work.
- Do not restore removed legacy paths, aliases, or shared headers.
- Do not commit generated local state under `.rklings/`.
- Do not add `Co-Authored-By` attribution lines.
- Do not extract basenames with `strrchr` in the mkdir interception path; the
  command is always a bare name, so compare `buf` directly against the prefix.
- Supplied hook code must match the same simplification: no `const char *base`,
  no `strrchr`, no `base ? base + 1 : buf` pattern.

## Documentation rules

- Use simple, direct language and introduce a term before relying on it.
- Explain the practical problem before presenting a data structure or API.
- Keep one idea per paragraph.
- Use real user commands (`ls`, `cat`, `id`, `grep`) in README examples, not
  shell assertions.
- Do not leak test infrastructure into learner prose.
- Do not use em dashes.
- Do not add emojis.
- Do not mention APIs the learner does not use.
- Every error-path TODO step must name the resources it releases before
  returning.

Never load a learner module into the host kernel.
