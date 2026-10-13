# Assemble and verify the complete rootkit

This capstone wires every operational milestone into one kernel module
with a unified operator interface. The work is integration: one startup
order, one rollback path, and one clean unload.

```text
operator -----> AEAD control -----> authenticated agent (remote)
                     |
                     +--> hide file/pid     (M03: getdents64 probe)
                     +--> hide content      (L15: ksys_read probe)
                     +--> hide port         (L26: tcp4/tcp6_seq_show probes)
                     +--> hide module       (M01: list_del/list_add)
                     +--> unhide module     (M01: list_add restore)
                     +--> escalate PID      (M02: remote cred replace)
                     +--> persist           (M00: modules-load.d)
                     +--> shell             (M04: UMH reverse shell)
                     +--> status            (agent state + target lists)

local user ---> mkdir rklings.X ----> mkdir kprobe (local)
                     |
                     +--> rklings.e          escalate calling process
                     +--> rklings.h.m        hide module
                     +--> rklings.u.m        unhide module
                     +--> rklings.h.f.NAME   hide file prefix
                     +--> rklings.h.i.PID    hide process
                     +--> rklings.h.c.STR    hide content
                     +--> rklings.h.p.PORT   hide port
                     +--> rklings.persist    stage boot-load
```

## Remote operator commands

All commands travel over the AES-256-GCM authenticated channel (M05):

| Command | Wire protocol | Effect |
|---------|---------------|--------|
| hide file PREFIX | HIDE FILE PREFIX | Hide matching files from directory listings |
| hide pid PID | HIDE PID PID | Hide PID from /proc |
| hide content STRING | HIDE CONTENT STRING | Remove matching lines from file reads |
| hide port PORT | HIDE PORT PORT | Hide port from /proc/net/tcp |
| unhide CATEGORY N | UNHIDE CATEGORY N | Remove target at index N |
| unhide all | UNHIDE ALL | Clear all hidden targets |
| hide module | HIDE MODULE | Hide module from lsmod |
| unhide module | UNHIDE MODULE | Restore module visibility |
| escalate PID | ESCALATE PID | Escalate process to root |
| persist | PERSIST | Stage boot-load request |
| shell | SHELL port | Open interactive root Bash |
| status | STATUS | Show agent state |

## Local commands

The mkdir kprobe (M02) provides a local control channel that works
without network access. Any user on the system can issue commands:

| Command | Effect |
|---------|--------|
| mkdir rklings.e | Escalate calling process to root |
| mkdir rklings.h.m | Hide module from lsmod |
| mkdir rklings.u.m | Unhide module |
| mkdir rklings.h.f.NAME | Hide file prefix |
| mkdir rklings.h.i.PID | Hide PID |
| mkdir rklings.h.c.STR | Hide content string |
| mkdir rklings.h.p.PORT | Hide port |
| mkdir rklings.persist | Stage boot-load request |

## OPSEC

The complete rootkit leaves no trace in dmesg. Zero printk calls.
The proc interface is at `/proc/rkling_status` (read-only, mode 0400).
On init, the module:

1. Adds "rkling" to the file target list (hides the .ko, proc entry,
   and any file starting with "rkling" from directory listings).
2. Adds the agent kthread PID to the PID target list (hides from ps).
3. Names the agent thread `kworker/u8:3` (blends with real workers).
4. Hides itself from lsmod (`list_del`).

The agent auto-hides its C2 connection port after `kernel_connect`
and removes it on disconnect before reconnect, so the ephemeral port
never appears in `/proc/net/tcp` or `/proc/net/tcp6`.

## Init sequence

The module has one load order and one teardown order. Both must be
correct: a failed step rolls back everything before it.

```text
init:                               exit (reverse):
  parse key + peer address            unhide module
  init target lists                   remove /proc/rkling_status
  init shell, start agent             unregister probes (all five)
  register probes (all five)          stop agent + shell
  create /proc/rkling_status          zeroize key
  auto-stealth (hide "rkling", PID)
```

The five probes, in registration order: `getdents64`, `ksys_read`,
`tcp4_seq_show`, `tcp6_seq_show`, `__x64_sys_mkdir`.

## Techniques introduced in this capstone

Three patterns appear here for the first time, building on earlier lings.

### Deferred credential replacement

L10 teaches `prepare_kernel_cred` + `commit_creds` for self-escalation:
the calling process replaces its own credentials. Remote escalation
(escalating a different PID) cannot call `commit_creds` from the agent
kthread, because `commit_creds` operates on `current`. On kernels with
CET/IBT (Control-flow Enforcement Technology), directly swapping another
task's credential pointer also triggers a control-protection fault.

The solution: pre-allocate root credentials in the agent context, store
them with an atomic PID flag, and let the target process pick them up
on its next `ksys_read` call. The read entry handler checks
`task_tgid_vnr(current)` against the stored PID, and if it matches,
calls `commit_creds(escalate_cred)` in the target's own context.

An `smp_wmb()` after storing the cred pointer (before the PID flag)
and `smp_rmb()` after reading the PID flag (before using the pointer)
ensure correct ordering on SMP systems. L05 and L06 link the kernel
memory-barriers documentation; this is the first time a barrier appears
in exercise code.

### Namespace-aware PID translation

L08 introduces `task_pid_nr()` for global PID numbers. The deferred
escalation check uses `task_tgid_vnr(current)` instead, which returns
the thread-group ID as seen from the task's PID namespace. This matches
what `ps` and the operator see, which is what matters when the operator
sends `ESCALATE <pid>`.

### IPv6 port hiding

L26 hooks `tcp4_seq_show` to hide ports from `/proc/net/tcp`. Many
listeners (including BusyBox `nc`) bind on IPv6 by default (`:::port`),
which appears in `/proc/net/tcp6` via `tcp6_seq_show`. The same entry
handler works for both: `inet_sk(sk)->inet_sport` gives the source
port regardless of address family.

### Thread name disguise

The agent kthread is named `kworker/u8:3` to blend with real kernel
worker threads in `ps` output. This is an OPSEC detail, not a new API.

## Your task

In `core/main.c`, implement `rk_init` and `rk_exit`.

Run:

```sh
./rklings check
```

## References

Refer to the README of each milestone (M00 through M06) for the kernel
APIs and techniques used in that subsystem.
