# L09: Route Multiple `mkdir` Commands

## Mission

L08 can carry one command through `mkdir`.
Now the rootkit needs several commands without one growing chain of special
cases.

Route `ping`, `status`, and `e` to separate handlers through one command table.

## What you will observe

Each command succeeds without creating a directory:

```console
root@victim:~# mkdir rklings.ping
root@victim:~# dmesg | tail -1
rklings: pong
root@victim:~# mkdir rklings.status
root@victim:~# dmesg | tail -1
rklings: alive, escalations=0
root@victim:~# ls rklings.ping rklings.status
```

`ls` finds neither command directory because both requests were consumed.

The existing escalation forms still work:

```text
rklings.e         elevate the parent shell
rklings.e.<pid>   elevate that PID
```

## Workspace

```text
exercises/L09-command-routing
├── Kbuild
├── main.c
├── rklings_command.c
├── rklings_command.h
├── rklings_escalation.c
├── rklings_escalation.h
└── rklings_hook.h
```

Only `rklings_command.c` is editable.
You split one command name and route it to the correct handler.

Supplied files intercept `mkdir`, change process credentials, and start and stop
the module.

## Read one command

The hook passes the complete basename to `rk_dispatch`.
The dispatcher removes `rklings.`, then splits the remaining text at its first
dot:

```text
rklings.ping        command="ping"    argument=NULL
rklings.status      command="status"  argument=NULL
rklings.e           command="e"       argument=NULL
rklings.e.1234      command="e"       argument="1234"
```

`strchr(command, '.')` finds the separator.
The bytes before it identify the command; the bytes after it are the optional
argument.

## Route through a table

Every handler has the same interface:

```c
typedef int (*rk_handler_t)(const char *arg);
```

Its return value becomes the result of the intercepted `mkdir`:

```text
0               command succeeded
negative errno  command failed
```

The command table pairs each exact name with its handler:

```c
struct rk_cmd_entry {
	const char *name;
	rk_handler_t handler;
};
```

The dispatcher compares both the command length and its bytes.
That prevents `e`, `extra`, and `e.1234` from being confused:

```text
e       -> handler e, no argument
e.1234  -> handler e, argument 1234
extra   -> no matching table entry
```

Unknown commands return `-ENOENT`. `ping` and `status` return `-EINVAL` when
given an argument.

## TODOs

```text
TODO 1  rklings_command.c:12:1  implement ping
TODO 2  rklings_command.c:18:1  implement status
TODO 3  rklings_command.c:24:1  preserve both escalation forms
TODO 4  rklings_command.c:37:1  map names to handlers
TODO 5  rklings_command.c:41:1  split and dispatch one command
```

### TODO 1 -- `rklings_command.c:12:1` -- Implement `ping`

Reject an argument with `-EINVAL`.
Otherwise log `rklings: pong\n` and return `0`.

### TODO 2 -- `rklings_command.c:18:1` -- Implement `status`

Reject an argument with `-EINVAL`.
Otherwise log `rklings: alive, escalations=%d\n` with `rk_escalation_count()`,
then return `0`.

### TODO 3 -- `rklings_command.c:24:1` -- Preserve escalation

| `arg`        | Target                       |
| ------------ | ---------------------------- |
| `NULL`       | `task_ppid_nr(current)`      |
| Decimal PID  | Parsed with `kstrtoint`      |
| Invalid text | Return the `kstrtoint` error |
| PID `<= 0`   | Return `-EINVAL`             |

Pass the selected PID to `rk_escalate_pid` and return its result.

### TODO 4 -- `rklings_command.c:37:1` -- Build the command table

Add exactly these entries to `rk_commands`:

```text
e       rk_cmd_escalate
ping    rk_cmd_ping
status  rk_cmd_status
```

### TODO 5 -- `rklings_command.c:41:1` -- Dispatch one command

1. Skip the `rklings.` prefix.
2. Find the first `.` with `strchr`.
3. Derive the command length.
4. Set the argument to the bytes after `.`, or `NULL` when there is no `.`.
5. Walk `rk_commands` with a `size_t` index.
6. Call a handler only when both the stored name length and `strncmp` match.
7. Return `-ENOENT` when no entry matches.

## Test it

From the host:

```console
$ ./rklings run L09
```

Inside the guest, load the module and run the commands from **What you will
observe**:

```console
root@victim:~# insmod /rklings/rklings.ko
root@victim:~# mkdir rklings.ping
root@victim:~# mkdir rklings.status
root@victim:~# su student
student@victim:~$ mkdir rklings.e
student@victim:~$ id
uid=0(root) gid=0(root) groups=0(root)
```

## Validate

From the repository root:

```console
$ ./rklings validate L09
```

Validation checks `ping`, `status`, parent and explicit-PID escalation, exact
command boundaries, ordinary directory creation, command return values, absent
command directories, and clean unload.

## Next

Commands can now trigger several immediate actions, but the module disappears on
reboot and anyone can find it in `lsmod`.
M00 adds boot persistence and module-list hiding.
