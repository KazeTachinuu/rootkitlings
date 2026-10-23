# L08: Build the `mkdir` Hook

## Mission

L07 changes a process's credentials, but supplied code still intercepts `mkdir`.

Now implement that interception and keep the existing escalation commands
working.

## What you will observe

The command succeeds, changes the shell's credentials, and creates no directory:

```console
student@victim:~$ mkdir rklings.e
student@victim:~$ id
uid=0(root) gid=0(root) groups=0(root)
student@victim:~$ ls rklings.e
```

`ls` finds no command directory because the hook replaced the syscall result.

The module also registers probes on both syscall entry points:

```console
root@victim:~# grep -E '__x64_sys_mkdir(at)?' /sys/kernel/debug/kprobes/list
... __x64_sys_mkdir+0x0 ...
... __x64_sys_mkdirat+0x0 ...
```

## Workspace

```text
exercises/L08-mkdir-hook
├── Kbuild
├── main.c
├── rklings_command.c
├── rklings_command.h
└── rklings_hook.h
```

Only `rklings_hook.h` is editable.
You implement the `mkdir` hook that L05-L07 previously supplied.

The other files start and stop the hook, parse commands, and change process
credentials.

## Follow one `mkdir` call

A kretprobe intercepts both the entry and return of a kernel function.
The API has been stable since 2004 and works on every architecture Linux
supports.

A kretprobe runs one callback when a kernel function begins and another when it
returns:

```text
mkdir("rklings.e")
        |
        v
entry handler
    copy the pathname
    run the supplied command
    save its result
    replace pathname[0] with '\0'
        |
        v
real mkdir sees an empty path and creates nothing
        |
        v
return handler
    replace ENOENT with the saved command result
        |
        v
userspace receives success
```

Each active call gets its own `instance->data` storage.
Here that storage is a `struct rk_call`: `intercepted` says whether the return
value must be replaced, and `result` holds the value to return.

Both callbacks return `0`.
For an entry handler, that means "track this call and run its return handler."
For a return handler, it means "resume normal execution."

## Reach the pathname

This course targets x86-64 syscall wrappers.
The entry callback receives a register frame whose first argument points to the
saved syscall registers:

```text
probe regs -> argument 0 -> syscall regs
                              di -> mkdir pathname
                              si -> mkdirat pathname
```

Recover that inner frame with:

```c
syscall_regs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
```

The two entry handlers select different registers, then call the same
interception function.

## Intercept the pathname

The pathname lives in userspace memory. `strncpy_from_user` copies it into a
kernel buffer:

```c
copied = strncpy_from_user(buf, pathname, sizeof(buf));
```

Only `0 < copied < sizeof(buf)` is a complete C string.
Ignore faults, empty strings, and truncated paths.

Compare the buffer against `RK_PREFIX` with `strncmp`.
Non-matching names pass through to the real `mkdir` untouched.

When the prefix matches, the entry handler does two things:

1. Zero the first userspace byte with `put_user('\0', pathname)`.
   The real syscall then sees an empty path and creates nothing.
2. Run the command from the kernel copy and save its result.

The return handler later replaces the syscall error with that saved result, so
userspace sees success instead of `ENOENT`.

## Own both probes

Each `struct kretprobe` connects a symbol to the callbacks and their per-call
storage:

```c
static struct kretprobe probe = {
	.kp.symbol_name = "__x64_sys_mkdir",
	.entry_handler = rk_entry_mkdir,
	.handler = rk_return,
	.data_size = sizeof(struct rk_call),
	.maxactive = 8,
};
```

`maxactive` reserves storage for up to eight simultaneous calls.

Registration can stop halfway:

```text
register mkdir
    |
    └── register mkdirat fails -> unregister mkdir -> return the error
    |
    └── both succeed ----------> module owns both probes
```

On unload, unregister in reverse order: `mkdirat`, then `mkdir`.

## TODOs

```text
TODO 1  rklings_hook.h:20:1  intercept a pathname and save the command result
TODO 2  rklings_hook.h:27:1  extract the pathname from both syscall wrappers
TODO 3  rklings_hook.h:40:1  return the saved command result
TODO 4  rklings_hook.h:46:1  describe both kretprobes
TODO 5  rklings_hook.h:50:2  register both probes or roll back
TODO 6  rklings_hook.h:56:2  unregister both probes in reverse order
```

### TODO 1 -- `rklings_hook.h:20:1` -- Intercept one pathname

1. Set `call->intercepted` to `false`.
2. Copy `pathname` into a `RK_PATH_MAX` buffer with `strncpy_from_user`.
3. Return `0` if the copy failed or was truncated.
4. Return `0` if the buffer does not start with `RK_PREFIX`.
5. Zero the first userspace byte with `put_user('\0', pathname)`.
6. Return `0` if `put_user` fails.
7. Set `intercepted` to `true` and store `rk_handle_mkdir(buf)` in `result`.
8. Return `0`.

### TODO 2 -- `rklings_hook.h:27:1` -- Extract both pathname arguments

For each entry handler:

1. Recover the inner `struct pt_regs` with `regs_get_kernel_argument(regs, 0)`.
2. Return `0` if it is `NULL`.
3. Read the pathname from `di` for `mkdir` or `si` for `mkdirat`.
4. Cast it to `const char __user *` and pass it with `(void *)instance->data` to
   `rk_do_intercept`.

### TODO 3 -- `rklings_hook.h:40:1` -- Return the command result

1. Read `struct rk_call` from `(void *)instance->data`.
2. If `intercepted` is true, pass the saved result to `regs_set_return_value` as
   `(unsigned long)(long)call->result`.
3. Return `0`.

The cast preserves negative error values.

### TODO 4 -- `rklings_hook.h:46:1` -- Describe both kretprobes

Declare two static `struct kretprobe` values:

| Probe     | Symbol              | Entry handler      |
| --------- | ------------------- | ------------------ |
| `mkdir`   | `"__x64_sys_mkdir"` | `rk_mkdir_entry`   |
| `mkdirat` | `RK_MKDIRAT_SYMBOL` | `rk_mkdirat_entry` |

For both probes, use `rk_return` as the return handler, `sizeof(struct rk_call)`
as `data_size`, and `8` as `maxactive`.

### TODO 5 -- `rklings_hook.h:50:2` -- Register both probes or roll back

1. Register the `mkdir` probe; return its exact error on failure.
2. Register the `mkdirat` probe.
3. If step 2 fails, unregister `mkdir` and return the second error.
4. Return `0` after both registrations succeed.

### TODO 6 -- `rklings_hook.h:56:2` -- Unregister both probes

Unregister `mkdirat` first, then `mkdir`.
This reverses the successful registration order.

## Test it

From the host:

```console
$ ./rklings run L08
```

Inside the guest, load the module and inspect the probes:

```console
root@victim:~# insmod /rklings/rklings.ko
root@victim:~# grep -E '__x64_sys_mkdir(at)?' /sys/kernel/debug/kprobes/list
```

Then switch to the student and run the commands from **What you will observe**.
An ordinary `mkdir ordinary` must still create the directory normally.

## Validate


```console
$ ./rklings validate L08
  [+] Both probes preserve successful escalation and ordinary mkdir
  [+] The direct probes preserve explicit PID targeting
  [+] Second-probe failure rolls back the first probe
  PASS
```


## Next

The module can intercept two escalation commands directly.
L09 replaces the fixed parser with a command table and dispatcher.
