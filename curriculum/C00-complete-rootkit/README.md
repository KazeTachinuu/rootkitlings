# Assemble and verify the complete rootkit

This capstone wires every operational milestone into one kernel module
with a unified operator interface. The work is integration: one startup
order, one rollback path, and one clean unload.

```text
operator -----> AEAD control -----> authenticated agent (remote)
                     |
                     +--> hide file/pid     (M03: getdents64 probe)
                     +--> hide content      (M06: vfs_read probe)
                     +--> hide port         (M06: tcp4_seq_show probe)
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
On init, the module adds "rkling" to the file target list so the proc
entry, kernel threads, and .ko file are hidden by default.

## Init sequence

The module has one load order and one teardown order. Both must be
correct: a failed step rolls back everything before it.

```text
init:                               exit (reverse):
  parse key + peer address            unhide module
  init target lists                   remove /proc/rkling_status
  init shell, start agent             unregister probes (all four)
  register probes (all four)          stop agent + shell
  create /proc/rkling_status          zeroize key
  auto-stealth (hide "rkling")
```

## Your task

In `core/main.c`, implement `rk_init` and `rk_exit`.

Run:

```sh
./rklings check
```

## References

Refer to the README of each milestone (M00 through M06) for the kernel
APIs and techniques used in that subsystem.
