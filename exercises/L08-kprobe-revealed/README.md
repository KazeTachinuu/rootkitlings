# L08: Intercept `mkdir` with Kprobes

L05–L07 used a supplied header to intercept `mkdir`.
This exercise removes that layer and registers the two probes directly.

The resulting module preserves both L07 commands:

```text
rklings.e         elevate the parent shell
rklings.e.<pid>   elevate an explicit process
```

## What you will observe

After the module loads, the kernel reports probes on both syscall entry points:

```console
root@victim:~# grep '__x64_sys_mkdir' /sys/kernel/debug/kprobes/list
...
root@victim:~# grep '__x64_sys_mkdirat' /sys/kernel/debug/kprobes/list
...
```

Both are needed because different userspace paths may call `mkdir` or `mkdirat`.

## Workspace

The workspace contains only `Kbuild` and `main.c`.
PID lookup and credential replacement are already implemented.

You will implement:

- pathname copying and command detection;
- one pre-handler for each syscall wrapper;
- both `struct kprobe` declarations;
- registration rollback and reverse teardown.

## What a kprobe does

A kprobe attaches a callback to an existing kernel instruction address.
Here, the address is resolved from a kernel symbol name:

```text
register_kprobe(&rk_kprobe_mkdir)
                  |
                  v
+--------------------------------------+
| symbol: __x64_sys_mkdir              |
| callback: rk_pre_mkdir               |
+------------------+-------------------+
                   | fires on function entry
                   v
         rk_pre_mkdir(probe, regs)
```

Declare the relationship in `struct kprobe`:

```c
static struct kprobe rk_kprobe_mkdir = {
	.symbol_name = "__x64_sys_mkdir",
	.pre_handler = rk_pre_mkdir,
};
```

The pre-handler runs before the probed function continues.
Returning `0` means “resume normal execution.” A nonzero pre-handler result has
special instruction-flow semantics; it is not a syscall error code.
Every handler in this exercise returns `0`.

## Find the syscall arguments

Userspace makes this call:

```c
mkdir(pathname, mode);
```

On x86-64, `__x64_sys_mkdir` is a wrapper whose kernel-level argument is a
pointer to the saved userspace syscall registers.
The register frame received by the kprobe is therefore not yet the frame that
contains `pathname`:

```text
KPROBE FRAME                         SYSCALL FRAME
+--------------------------+         +--------------------------+
| argument 0               |         | di: mkdir pathname       |
|        |                 |         | si: mkdir mode           |
|        +-------------------------> |                          |
+--------------------------+         +--------------------------+

mkdirat uses the same inner frame:
+--------------------------+
| di: directory fd         |
| si: mkdirat pathname     |
| dx: mkdirat mode         |
+--------------------------+
```

Recover the inner frame first:

```c
uregs = (struct pt_regs *)regs_get_kernel_argument(regs, 0);
```

Then select the correct register:

```text
__x64_sys_mkdir      pathname = uregs->di
__x64_sys_mkdirat    pathname = uregs->si
```

Pass both paths to one shared `rk_do_intercept` function.

## Copy and validate the pathname

The pathname pointer belongs to userspace.
Copy it into a fixed kernel buffer before inspecting it:

```c
copied = strncpy_from_user(buf, pathname, sizeof(buf));
```

Handle all three outcomes:

```text
copied <= 0              fault or empty input; do not intercept
copied == sizeof(buf)    path was truncated; do not inspect it as a C string
0 < copied < sizeof(buf) complete null-terminated pathname is available
```

After a complete copy, find the final slash:

```text
/tmp/demo/rklings.e.1234
          +-------+------+
               basename
```

`strrchr(buf, '/')` returns the last slash.
The basename begins one byte later; when no slash exists, it begins at `buf`.

Only basenames beginning with `rklings.` enter the command path.
Ordinary pathnames return immediately and continue unchanged.

## Prevent the directory creation

A pre-handler cannot replace the syscall's eventual return value.
This lesson instead replaces the first byte of the caller's pathname with
`'\0'`:

```text
before put_user                    after put_user

pathname                           pathname
   |                                  |
   v                                  v
"/tmp/rklings.e\0"                 "\0tmp/rklings.e\0"
```

The command has already been copied into `buf`, so the module can still parse
it.
The real syscall resumes with an empty path and returns `ENOENT` without
creating a directory.

Check `put_user` before dispatching the command.
If the userspace byte cannot be written, do not perform the privileged action.

## Register both probes atomically

Probe registration is another owned-resource sequence:

```text
register mkdir probe
        |
        +-- failure ---------------> return error
        |
        v
register mkdirat probe
        |
        +-- failure ---------------> unregister mkdir probe
        |                            return error
        v
both probes active
```

Module exit unregisters `mkdirat` first and `mkdir` second.
After `unregister_kprobe` returns, that probe's callback will no longer execute.

## Implement it

Complete the TODOs in this order:

1. In `rk_do_intercept`, copy the pathname and reject faults or truncation.
2. Extract the basename and return for an ordinary prefix.
3. Null the first userspace byte with `put_user`; stop if it fails.
4. Log the suffix and pass the saved basename to `rk_handle_mkdir`.
5. In each pre-handler, recover the inner `pt_regs` and select `di` or `si`.
6. Declare the `mkdir` and `mkdirat` probes.
7. Register both with rollback after a partial failure.
8. Unregister both in reverse order during exit.

Do not include `rklings_hook.h`; this module owns the probes itself.

## Test it

Build and enter the VM:

```console
$ ./rklings run L08
```

Load the module, inspect registration, and trigger the parent-shell path:

```console
root@victim:~# insmod /rklings/rklings.ko
root@victim:~# grep -E '__x64_sys_mkdir(at)?' \
> /sys/kernel/debug/kprobes/list
root@victim:~# su student
student@victim:/root$ mkdir /tmp/rklings.e
mkdir: cannot create directory '/tmp/rklings.e': No such file or directory
student@victim:/root$ id -u
0
```

An ordinary directory must still be created:

```console
student@victim:/root$ mkdir /tmp/ordinary
student@victim:/root$ test -d /tmp/ordinary && echo created
created
```

## Validate

From the repository root:

```console
$ ./rklings validate L08
```

The checks inspect the submitted source for direct kprobe use, load and unload
the module, exercise the parent-shell and explicit-PID commands, and verify that
ordinary `mkdir` calls do not elevate a process.

## Next

The pre-handler can inspect and alter inputs, but it cannot report a successful
command after the real syscall returns `ENOENT`.
L09 uses a kretprobe pair to carry the handler result from function entry to
function return and replace the value seen by userspace.
