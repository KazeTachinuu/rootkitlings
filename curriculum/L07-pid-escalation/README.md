# Escalate another process by PID

L06 let you escalate the process that runs mkdir. But what if you want to
escalate a different process, one you do not control directly? This uses
deferred execution: set a target PID via mkdir, and the target process
picks up root credentials the next time it reads `/proc/rklings`.

## Deferred delivery with atomic flags

You cannot directly modify another process's credentials from your mkdir
handler. The credential API (`commit_creds`) only works on the calling
process. Instead, you store the target PID in an atomic variable:

```c
static atomic_t rk_target_pid = ATOMIC_INIT(0);
```

When a mkdir like `rklings.e.1234` arrives, you parse the PID and store
it with `atomic_set`. When process 1234 reads `/proc/rklings`, you check
if its PID matches the stored target, escalate it, and clear the flag.

## Architecture

```
  User A                         User B (pid 1234)
    |                                |
    | mkdir rklings.e.1234           |
    |----> RK_ON_MKDIR              |
    |      atomic_set(pid, 1234)     |
    |                                |
    |                    cat /proc/rklings
    |                    proc_read ---|
    |                    pid match?  yes
    |                    escalate + clear
    |                                |
    |                    id -> uid=0
```

## What to build

This module combines two subsystems:

1. The mkdir hook (from L05/L06): handles three cases:
   - `rklings.e` with no suffix: self-escalation (like L06)
   - `rklings.e.<pid>`: store the PID for deferred delivery
   - Anything else: return `-ENOENT`

2. A `/proc/rklings` file (from L03): on read, check if
   `current->pid` matches the stored target. If so, escalate
   and clear the atomic. Always print a status line.

Use `kstrtoint` to parse the PID string into an integer.

## Demo

```
$ echo $$
1234
$ # (from another terminal)
$ mkdir rklings.e.1234
mkdir: cannot create directory 'rklings.e.1234': No such file or directory
$ # (back in pid 1234's terminal)
$ cat /proc/rklings
rklings: escalated
$ id
uid=0(root) gid=0(root) ...
```

## Verification

The test verifies all three paths: self-escalation via `rklings.e`,
PID targeting via `rklings.e.<pid>`, and deferred delivery when the
target process reads `/proc/rklings`.
