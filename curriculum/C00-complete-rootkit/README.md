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

local user ---> mkdir rkling.X -----> mkdir kprobe (local)
                     |
                     +--> rkling.e          escalate calling process
                     +--> rkling.h.m        hide module
                     +--> rkling.u.m        unhide module
                     +--> rkling.h.f.NAME   hide file prefix
                     +--> rkling.h.i.PID    hide process
                     +--> rkling.h.c.STR    hide content
                     +--> rkling.h.p.PORT   hide port
                     +--> rkling.persist    stage boot-load
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
| mkdir rkling.e | Escalate calling process to root |
| mkdir rkling.h.m | Hide module from lsmod |
| mkdir rkling.u.m | Unhide module |
| mkdir rkling.h.f.NAME | Hide file prefix |
| mkdir rkling.h.i.PID | Hide PID |
| mkdir rkling.h.c.STR | Hide content string |
| mkdir rkling.h.p.PORT | Hide port |
| mkdir rkling.persist | Stage boot-load request |

## OPSEC

The complete rootkit leaves no trace in dmesg. Zero printk calls.
The proc interface is at `/proc/rkling_status` (read-only, mode 0400).
On init, the module adds "rkling" to the file target list so the proc
entry, kernel threads, and .ko file are hidden by default.

## Your task

In `core/main.c`, wire the init/exit sequence:

1. Parse control key and peer address
2. Init all four target lists (file, pid, content, port)
3. Init root shell, start authenticated agent
4. Register probes (getdents64, vfs_read, tcp4_seq_show, mkdir)
5. Create /proc/rkling_status
6. Auto-stealth: add "rkling" to file targets
7. On exit: uncloak, remove proc, unregister all probes, stop agent/shell

Run:

```sh
./rootkitlings check
```

## References

Refer to the README of each milestone (M00 through M06) for the kernel
APIs and techniques used in that subsystem.
