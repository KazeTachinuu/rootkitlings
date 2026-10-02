# 21 - Current task

## Why this step

Every kernel callback runs on behalf of a userspace process.
`current` is a macro that gives you the `task_struct` of the calling
process. A rootkit uses this to filter: only respond to the right caller.

## What you need

You already know `proc_create` and `simple_read_from_buffer` from
ling 08. This ling reads process information from `current`.

New includes:

```c
#include <linux/sched.h>   // current, task_struct
```

## The concept

```
  cat /proc/rk_task          shell (pid 42)
    |                        |
    v                        v
  rk_read()          current->pid  = 42
                     current->comm = "cat"
                     current->tgid = 42
```

`current` is always valid inside a process-context callback (like
your proc read). It points to the `task_struct` of whoever triggered
the call.

Key fields:
- `current->pid`: thread ID (matches what userspace calls the PID)
- `current->comm`: executable name, truncated to 16 characters

## Build it

Adapt your `/proc/rk_task` read callback from ling 08. Instead of a
static string, format `pid=N comm=NAME` using `current->pid` and
`current->comm`.

You need `linux/sched.h` for the `current` macro and `task_struct`.

## Predict

- What is `current->comm` when you `cat /proc/rk_task`?
- Does `current` change between two reads in the same `cat`?
- What is `current` inside a timer callback?

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_task       # pid=<N> comm=cat
```


## Contract

The check script verifies:

- `/proc/rk_task` must output lines in this format:
  ```
  pid=N comm=PROCESS_NAME
  ```
- `/proc/rk_task` must contain `pid=` and `comm=` fields from `current`.

## Verify

```bash
./rootkitlings check 21
```

> **What a defender sees**: nothing new. But the module now knows WHO is
> asking. This is the prerequisite for caller filtering.

See also: [22 - Credentials](../22-credentials/README.md).
