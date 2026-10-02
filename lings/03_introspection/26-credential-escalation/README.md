# 25 - Credential escalation

## Why this step

The Diamorphine rootkit backdoor: send a magic command to any process
and it becomes root. This is the classic privilege escalation primitive,
and it all comes down to two kernel functions.

```
  echo "escalate" > /proc/rk_escalate

  +------------------+
  | calling process  |
  | uid=1000 (user)  |
  +--------+---------+
           |
           v
  +------------------+     +------------------+
  | prepare_creds()  | --> | new cred (copy)  |
  | (copies current) |     | uid=1000         |
  +------------------+     +--------+---------+
                                    |  set all uid/gid
                                    |  fields to 0
                                    v
                           +------------------+
                           | commit_creds()   |
                           | (replaces live)  |
                           +------------------+
                                    |
                                    v
                           +------------------+
                           | calling process  |
                           | uid=0 (root)     |
                           +------------------+
```

## What you already know

From ling 24: `current_cred()`, `from_kuid`, reading the caller's UID.
From ling 12: writable proc entries with `copy_from_user`.

## New API

Same headers as ling 24 (`linux/cred.h`, `linux/uidgid.h`):

| API | Purpose |
|-----|---------|
| `prepare_creds()` | Copy the calling task's credentials into a new mutable `struct cred` |
| `commit_creds(new)` | Atomically replace the calling task's credentials |
| `GLOBAL_ROOT_UID` | The UID value for root (0) |
| `GLOBAL_ROOT_GID` | The GID value for root (0) |

The `struct cred` has eight identity fields you need to set:
`uid`, `gid`, `euid`, `egid`, `suid`, `sgid`, `fsuid`, `fsgid`.

Note: older rootkits used `prepare_kernel_cred(NULL)` which created
an all-powerful credential from scratch. Modern kernels (5.x+) require
`prepare_creds()` with manual field setting.

## Build it

Create a module that:

1. Creates a writable `/proc/rk_escalate` (mode `0666`).
2. Writing `"escalate"` gives the calling process root (UID 0).
3. Reading returns the caller's current UID (reuse ling 24 pattern).

The write handler should parse the command (ling 12 pattern), and only
escalate when the input starts with `"escalate"`. Log which PID was
escalated.

## Try it

```bash
./rootkitlings run
```

Inside the VM:

```sh
insmod hello.ko
cat /proc/rk_escalate               # uid=0
printf 'escalate' > /proc/rk_escalate
dmesg | tail
rmmod hello
```

## Contract

The check script verifies:

- `/proc/rk_escalate` is writable (mode 0666).
- Reading shows `uid=` field from current credentials.
- Writing a string starting with `e` triggers escalation to `uid=0`.
- `/proc/rk_escalate` disappears after unload.

## Verify

```bash
./rootkitlings check 23
```

> **What a defender sees**: `audit.log` records every UID 0 transition.
> `ausearch -m USER_AUTH` finds it. The escalation itself is instant and
> invisible to tools like `ps`, but the audit subsystem catches it.

See also: [24 - Symbols](../24-symbols/README.md).
