# R02 — Build, load, inspect, and unload a module

You are starting the permanent rootkit source tree.
In this exercise you own its external-module build, lifecycle callbacks, and static identity.
The other files are deliberately inert placeholders for later Stage 1 work.

## Your task

Edit `Kbuild` and `core/main.c` so that:

- `Kbuild` produces `rootkitlings.ko` from `core/main.o`, `core/state.o`, and
  `status/status.o`, includes `include/`, and defines the Stage 1 failure-point
  build value with a default of zero;
- `core/main.c` registers successful init and exit callbacks;
- the module declares GPL licensing and the description
  `Rootkitlings inspectable core`;
- the supplied header and inactive component files remain unchanged; they are
  fixed cumulative placeholders, not work for this lesson.

Build warnings are failures.
You may log for your own diagnosis, but the checker does not treat a log message as proof of lifecycle or cleanup.

## Check

From the repository root, run:

```bash
curriculum/incubator/stage-01/r02-module-lifecycle/check path/to/workspace
```

The host phase verifies the inactive files, performs clean warning-enabled builds, checks the failure-point build input, and inspects the resulting module metadata and lifecycle symbols.
The disposable guest then proves that kernel inventory and `/sys/module/rootkitlings` agree after load, disappear after unload, and work again on an immediate reload.

Success leaves no loaded module or persistent guest state.
