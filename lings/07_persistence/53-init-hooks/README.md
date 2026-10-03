# 53 - Usermode helper

## Why this step

`call_usermodehelper` asks the kernel to start a userspace program. The helper
inherits kernel-selected credentials and environment, and the caller chooses
whether to wait for completion. The resulting process is visible through the
normal process, audit, and execution telemetry.

## What you need

From ling 11 you know `proc_create_single` and `seq_file`.

New include:

```c
#include <linux/umh.h>   // call_usermodehelper, UMH_WAIT_PROC
```

## Build it

Create a module that:

1. On load, runs `touch /tmp/rootkit_was_here` via the kernel.
2. `/proc/rk_persist` shows the command and its exit code.

Key code:

```c
char *argv[] = { "/bin/sh", "-c", "touch /tmp/rootkit_was_here", NULL };
char *envp[] = { "PATH=/bin:/sbin", NULL };
int ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
```

`UMH_WAIT_PROC` blocks until the command finishes. Alternatives:

- `UMH_WAIT_EXEC`: return after exec, don't wait for exit
- `UMH_NO_WAIT`: fire and forget

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
ls /tmp/rootkit_was_here    # exists
cat /proc/rk_persist             # cmd=..., exit=0
rmmod hello
```

## Contract

The check script verifies:

- `/tmp/rootkit_was_here` exists after loading (created by `call_usermodehelper`).
- `/proc/rk_persist` contains `cmd=touch /tmp/rootkit_was_here` and `exit=0`.
- `/proc/rk_persist` disappears after unload.

## Verify

```bash
./rootkitlings check 53
```

> **What a defender sees**: the process is not launched by an interactive
> shell, but `/proc`, audit/execution monitoring, filesystem effects, and
> external observation can still record it.

See also: [54 - File infection](../54-file-infection/README.md).
