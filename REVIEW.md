# Exercise Review Protocol

This document is the canonical procedure for reviewing and refining every
Rootkitlings exercise.
Its purpose is to make each lesson practical, minimal, and independently
understandable from the knowledge established earlier in the course.

Review the complete learner experience, not only the prose:

```text
previous lesson -> starter -> README -> implementation
implementation -> validation -> next lesson
```

An exercise is finished only when that entire path is coherent and proven.

## Quality bar

Every exercise must have one clear operational outcome.
The learner should know what they are building, observe it working, and
understand the small set of new ideas needed to complete it.

Apply these rules throughout the review:

- Teach only knowledge required by the exercise's outcome.
- Do not assume an API, language feature, tool, or runtime boundary that the
  learner has not already used.
- Explain a new detail at the point where the learner needs it.
  Do not insert a generic C, Go, Linux, networking, or kernel tutorial.
- Supply testing machinery, repetitive declarations, and unrelated plumbing.
  Do not turn them into student work.
- Keep one coherent decision or lifecycle step per TODO.
- Make failure behavior as explicit as success behavior when it affects
  ownership, cleanup, protocol state, or security.
- Validate observable behavior.
  A passing build alone is not proof of a runtime contract.
- Remove claims that the validator does not actually prove.
- Prefer fewer precise sentences over inventories of terminology.
- Explain why the exercise matters to the rootkit being built, using one
  concrete capability or consequence rather than generic motivation.
- State current limits honestly; do not call a mechanism hidden, safe, or
  complete before the implementation and validation prove it.

Never preserve a mechanism merely because it is already in the reference
solution.
The solution is evidence to inspect, not a specification that cannot be
corrected.

## Exercise artifacts

For an exercise `LXX-name`, inspect all applicable artifacts:

```text
course.toml
exercises/LXX-name/README.md
exercises/LXX-name/Kbuild or other build files
exercises/LXX-name/<editable starter files>
exercises/LXX-name/.rklings/exercise.toml
exercises/LXX-name/.rklings/solution/**
exercises/LXX-name/.rklings/checks/**
exercises/LXX-name/.rklings/fixtures/**
shared tools, libraries, and harnesses named by the manifest
the previous README's Next section
the next README's Mission section
```

The manifest is the source of truth for workspace files, editable files,
prerequisites, supplied mechanisms, and validation cases. `rklings validate` is
the only supported validation entrypoint; do not create a second boot or
self-test framework.

## Review procedure

Complete the following steps in order for each exercise.

### 1. State the lesson boundary

Read the curriculum route, the previous transition, the manifest, all starter
files, the entire solution, every check and fixture, and the next transition.
Then write these two sentences in the review record:

```text
Core outcome: After this exercise, the learner can ...
Remaining limitation: The result still cannot ...
```

The core outcome must describe one observable capability, not a list of APIs.
The remaining limitation must naturally motivate the next exercise.

If the implementation contains work unrelated to that outcome, choose one of
three actions:

1. Remove it if it serves no later or runtime purpose.
2. Supply it if it is necessary plumbing but not the lesson.
3. Move it to the exercise where it becomes relevant.

Do not expand the README to justify accidental complexity.

### 2. Audit the knowledge boundary

List every concept the student must actively use: language constructs, kernel or
Go APIs, build syntax, error conventions, ownership rules, protocols, commands,
and host/guest boundaries.

Classify each item exactly once:

<!-- markdownlint-disable MD013 -->

| Classification | Requirement                                                  |
| -------------- | ------------------------------------------------------------ |
| Prior          | Introduced and exercised by a prerequisite.                  |
| New            | Required here and explained beside its use.                  |
| Supplied       | Present in the workspace but not implemented by the student. |

<!-- markdownlint-enable MD013 -->

Resolve every unclassified item.
Either teach it locally, supply it, or remove it.
Do not silently rely on “basic knowledge.” Conversely, do not reteach a prior
concept unless this exercise changes its contract.

Update the manifest after the classification:

- `prerequisites` names the exercises that establish the required baseline;
- `requires` names concepts genuinely used as prior knowledge;
- `teaches` contains only knowledge the student practices here;
- `supplied` plainly names important mechanisms the learner can rely on but does
  not implement.

Internal concept identifiers belong in the manifest, not in learner prose.

### 3. Derive student work from the code

Compare every editable starter file with its reference counterpart.
Read both sides of the diff; TODO comments alone are not authoritative.

For each meaningful difference, decide whether it is:

- a student decision required by the core outcome;
- necessary supplied scaffolding;
- generated or irrelevant noise;
- stale behavior that should be deleted from both sides.

Then design the smallest complete TODO sequence.
TODOs must follow the order in which a learner should implement them, even when
that crosses files.

A valid TODO:

- owns one coherent operation, decision, or lifecycle transition;
- has enough surrounding skeleton to make its location and inputs obvious;
- does not reveal the finished implementation in its source comment;
- is large enough to teach something and small enough to verify;
- is implemented by the solution with no unexplained code appearing elsewhere.

Use one-line numbered source comments:

```c
// TODO 3: release the resources in reverse order
```

Avoid recipe comments such as:

```c
// TODO 3: call destroy(b), set b to NULL, then call destroy(a)
```

Number TODOs from 1 with no gaps or duplicates.

### 4. Build the synchronization table

Create one row per TODO while reviewing.
This may live in the review notes; it does not belong in the learner README.

<!-- markdownlint-disable MD013 -->

| TODO | Starter coordinate   | README requirement | Solution behavior    | Validation proof                     |
| ---- | -------------------- | ------------------ | -------------------- | ------------------------------------ |
| 1    | `file.c:line:column` | Exact learner task | Exact implementation | Case/assertion that fails without it |

<!-- markdownlint-enable MD013 -->

A row passes only when all five cells describe the same responsibility.

Check specifically that:

- the coordinate points to the numbered source comment;
- the README requests exactly what the solution does;
- success and relevant failure outcomes are stated;
- omitting or incorrectly implementing the TODO causes validation to fail;
- validation does not depend on undefined behavior or incidental log noise.

Compilation is sufficient proof for declarations and type contracts.
Runtime behavior, ordering, cleanup, state transitions, and security properties
need focused behavioral assertions.

### 5. Simplify the implementation

Before polishing prose, remove unnecessary complexity from the starter,
solution, and checks.

Look for:

- unused fields, functions, branches, fixtures, and failure points;
- testing infrastructure exposed as lesson content;
- duplicated state or values that can disagree;
- defensive code for impossible states that obscures the actual invariant;
- an API introduced before any real caller needs it;
- logs used as a substitute for observable system behavior;
- boilerplate that can be supplied without hiding the lesson;
- legacy commands, paths, harnesses, or terminology.

Minimal does not mean incomplete.
Keep every check and explanation necessary to make ownership, error propagation,
cleanup, protocol framing, concurrency, and security behavior correct.

### 6. Rewrite the README as a working guide

Use this order:

```text
# LXX: Concrete outcome
## Mission
## What you will observe
## Workspace
[only the mechanism sections needed here]
## TODOs
## Test it
## Validate
## Next
```

`D00` and `P00` are operational exercises and may omit `TODOs` when they do not
ask the learner to edit code.

Each section has one job:

- **Mission:** begin with the concrete operational problem.
  Show what the current implementation cannot do, what capability the learner
  will add, and where the next component will use it.
  A learner must understand why the new code exists before seeing its
  representation or API.
- **What you will observe:** show stable, concrete success and important failure
  results before implementation detail.
- **Workspace:** show the real tree and distinguish editable work from supplied
  machinery.
- **Mechanism:** explain only the new model or API needed by the TODOs.
  Follow the problem's execution path.
  Show the state before the operation, the exact state change, the kernel or
  program behavior that consumes the changed state, and how that produces the
  observable result.
  Never begin with a data structure, API catalogue, or concurrency rule before
  establishing the need it serves.
- **Tradecraft context:** where useful, connect the lesson to one concrete
  rootkit capability under a natural, exercise-specific heading.
  Keep it short and do not repeat the Mission, mechanism, or Next section.
- **TODOs:** provide the implementation path without pasting the solution.
- **Test it:** give the shortest useful manual feedback loop and state whether
  each command runs on the host or in the guest.
- **Validate:** describe only properties actually asserted by manifest cases.
- **Next:** name the remaining limitation that the following lesson resolves.

Delete duplicated explanations, concept inventories, motivational filler,
generic warnings, internal implementation history, and unstable output.
Use a table or diagram only when it makes a lifecycle, boundary, mapping, or
data flow clearer than short prose.

Apply the same explanation pattern to every section, not only the Mission:

1. Begin with the concrete action, problem, or result the learner already sees.
2. State why the next mechanism is needed.
3. Introduce only the API or rule that solves that problem.
4. Define a new term in plain language when it first appears.

Keep one idea per paragraph.
Prefer ordinary words such as “remember,” “read,” “remove,” and “running
process” until the kernel term itself is necessary.
Do not make the learner decode phrases such as “runtime state,”
“userspace-readable view,” or “interception layer” before showing what they mean
in practice.

### 7. Write practical TODO explanations

Start the TODO section with a compact navigation map using current coordinates:

```text
TODO 1  core/main.c:24:2  acquire state or reject the load
TODO 2  core/main.c:39:2  release the owned state
```

Then give each TODO a matching heading:

```markdown
### TODO 1 — `core/main.c:24:2` — Acquire state or reject the load
```

Under the heading, use the smallest explanation that lets a prepared learner act
correctly:

1. State the intended outcome in plain language.
2. Show a small outcome table only when success and failure diverge.
   Prefer two direct mapping lines for a simple two-case contract; do not add a
   table merely to decorate short prose.
3. Use numbered steps for operations whose order matters.
4. Remove any sentence that only restates the heading, mapping, or steps.
5. Give ordered implementation steps when order matters.
6. Explain one non-obvious API rule or invariant when needed.

Do not print artificial labels such as `What`, `How`, `Why`, or `Contract`.
Do not repeat a mechanism already explained earlier.
A straightforward TODO may need only two sentences.

Do not reduce a mechanism to a call sequence.
For every non-obvious operation, the learner must be able to answer:

1. What object or state exists before the call?
2. What does the call allocate, modify, register, or publish?
3. Which later code reads that changed state?
4. Why does that read produce the observed behavior?
5. What remains unchanged or unsupported?

Mention exact API names, arguments, return conventions, ownership transfer, and
cleanup order when correctness depends on them.
Avoid vague instructions such as “handle errors,” “set things up,” or “clean
everything.”

### 8. Audit validation adversarially

Read the implementation of every declared check.
A reference pass proves only that the reference passes; it does not prove that a
broken student solution is rejected.

For the exercise's central contract, establish the appropriate positive and
negative evidence:

- successful behavior is observed through the real public interface;
- exact errno or protocol response is checked where meaningful;
- failed initialization leaves no module, resource, registration, or partial
  state behind;
- successful teardown releases owned resources in valid order;
- repeated operations do not expose stale state;
- invalid build variants or inputs are rejected when claimed;
- configurable values are proven with at least two meaningful values;
- concealment is paired with a control proving the hidden object still exists;
- network and cryptographic claims use the real transport and peer, not mocks.

Do not claim a property in `Validate` unless a check asserts it.
Either improve the check or narrow the claim.

Ensure every check is declared by a manifest and every declared path exists.
Keep fixtures focused on one plausible wrong implementation.

### 9. Walk the learner path

Read the rendered README from top to bottom with only the declared prior
knowledge.
Follow its TODOs against a clean starter workspace.

Verify that:

- no term is used before it is introduced;
- no required code arrives magically between steps;
- commands identify host versus guest context;
- copied commands and expected outputs are accurate;
- TODO order matches implementation dependencies;
- manual testing is quick and does not duplicate the complete validator;
- the transition from the previous and into the next exercise is truthful.

If the README needs oral explanation from its author, it is not finished.

### 10. Run acceptance

From the repository root, run at least:

```console
$ rg -n 'TODO [0-9]+' exercises/LXX-name
$ diff -ru --exclude=.rklings \
    exercises/LXX-name \
    exercises/LXX-name/.rklings/solution
$ ./rklings validate LXX --reference
$ make check
$ git diff --check
```

Adapt the diff so it compares learner-owned files without generated outputs or
internal validation artifacts obscuring the result.
Run focused tests for any shared code changed during the review.

Do not mark the exercise complete because formatting or the reference build
passes.
All synchronization and proof requirements still apply.

## Review record

Produce this record for every exercise:

```markdown
### LXX — Title

Core outcome: Remaining limitation:

Prior knowledge:

- ...

New knowledge:

- ...

Supplied machinery:

- ...

TODO count:

Synchronization fixes:

- ...

Complexity removed:

- ...

Validation gaps fixed:

- ...

Commands passed:

- ...

Remaining concern: none | precise blocker
```

Keep review records outside learner READMEs.
They describe editorial work, not the lesson.

## Parallel review rules

When several agents review exercises concurrently:

1. Assign each exercise to exactly one agent.
2. Give that agent ownership of its directory only.
3. Reserve shared files such as `course.toml`, `CLAUDE.md`, `Makefile`, `vm/`,
   `internal/`, and shared libraries for the coordinating agent.
4. Require each agent to return its review record and exact commands run.
5. Integrate exercises in curriculum order so prerequisite and transition
   changes can be checked at their boundaries.
6. Run the repository-wide checks only after all exercise-local work is merged.

An agent must not infer or rewrite the intent of adjacent exercises in
isolation.
It should report a boundary conflict to the coordinator.

## Definition of done

An exercise is ready only when all of the following are true:

- Its core outcome is singular, practical, and observable.
- Its remaining limitation leads honestly to the next exercise.
- Every required idea is prior, taught here, or explicitly supplied.
- Student work contains no unrelated plumbing or ceremonial boilerplate.
- TODO numbering, coordinates, prose, solution, and validation agree exactly.
- Completing the documented TODOs produces the complete reference behavior.
- Every central success, failure, ownership, and cleanup claim is proven.
- The manual path and all automated checks pass.
- The README is concise enough to use while coding and complete enough to use
  without an instructor filling gaps.
- Required technical terms are introduced in plain language; abstract jargon,
  tautologies, and repeated explanations are absent.
- No obsolete command, path, harness, artifact, or terminology remains in the
  learner path.
- Tradecraft context states one real purpose and does not exaggerate the
  exercise's current stealth or capability.
- The review record has no unresolved concern.

If any item fails, the exercise is still under review.
