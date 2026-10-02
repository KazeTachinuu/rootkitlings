# 49 - Usermode helper

## Why this step

A rootkit in kernel space can execute any userspace command.
`call_usermodehelper` spawns a process as root with no login, no shell
history, no audit trail from the user's perspective.

Real rootkits use this to add SSH keys, create backdoor users, or
download payloads. Diamorphine triggers it via a magic signal.
Reptile uses it to launch reverse shells.

## What you need

From ling 09 you know `proc_create_single` and `seq_file`.

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
./rootkitlings check 49
```

> **What a defender sees**: the process is parented to kthreadd (PID 2),
> not any user shell. It bypasses utmp/wtmp. The only trace is in
> `/proc` (briefly) and the command's side effects. Audit rules on
> `execve` catch it.

See also: [50 - File infection](../50-file-infection/README.md).
