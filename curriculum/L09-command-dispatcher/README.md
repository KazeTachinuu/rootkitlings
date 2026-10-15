# Build a command dispatcher with kretprobe

In L08 you used a plain kprobe. It fires before the function runs, so you
can act on the arguments, but you cannot control the return value. The real
mkdir still returns an error (ENOENT from the nullified path) and the caller
sees a failure even though your action succeeded.

A **kretprobe** fixes this. It fires two handlers as a matched pair:

```
userspace: mkdir("rklings.ping")
       |
       v
__x64_sys_mkdir entry
       |
  [entry_handler fires]
       |  - extract pathname, parse command
       |  - dispatch to handler, store result
       |  - nullify pathname
       |  - set intercepted = true
       |
       v
__x64_sys_mkdir runs (empty path, fails)
       |
  [return handler fires]
       |  - check intercepted flag
       |  - regs_set_return_value(regs, our_result)
       |
       v
userspace sees return code 0 (success)
```

## Per-instance data

Each kretprobe firing gets its own `kretprobe_instance`. The `data` field
is a private buffer sized by `.data_size` in the kretprobe struct. Cast it
to your own type to pass state from entry to return:

```c
struct rk_mkdir_data {
    int result;
    bool intercepted;
};
```

## Command dispatch

Parse the basename after "rklings." into a command and optional argument,
separated by a dot:

```
rklings.ping        -> cmd="ping",   arg=NULL
rklings.status      -> cmd="status", arg=NULL
rklings.e           -> cmd="e",      arg=NULL
rklings.e.1234      -> cmd="e",      arg="1234"
```

Use `strchr` on the suffix to find the dot. Compare the command length and
string to dispatch:

| Command | Argument | Action |
|---------|----------|--------|
| e       | (none)   | Escalate current process |
| e       | PID      | Escalate target PID |
| ping    | (none)   | pr_info acknowledgment |
| status  | (none)   | pr_info with escalation count |

## Overriding the return value

In the return handler, use `regs_set_return_value` to replace the syscall
return code:

```c
regs_set_return_value(regs, (unsigned long)(long)data->result);
```

Cast through `long` first to sign-extend negative error codes properly.

## Your task

1. Define `struct rk_mkdir_data` for per-instance state
2. Write command handlers: `rk_cmd_escalate`, `rk_cmd_ping`, `rk_cmd_status`
3. Write `rk_dispatch` to parse "rklings.<cmd>.<arg>" and route
4. Write `rk_mkdir_entry` (extract path, parse, dispatch, nullify)
5. Write `rk_mkdir_return` (override return value if intercepted)
6. Define the `struct kretprobe` for `__x64_sys_mkdir`
7. Register and unregister in init/exit

## Verify

```sh
$ mkdir rklings.ping && echo "ok"
ok
$ dmesg | tail -1
rklings: dispatch: cmd=ping
$ mkdir rklings.e && id
uid=0(root)
```

Run:

```sh
./rklings check
```
