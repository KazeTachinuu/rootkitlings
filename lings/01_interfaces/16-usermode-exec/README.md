# 16 - Usermode exec

Kernel modules can execute userspace programs. `call_usermodehelper`
spawns a process from kernel context, running as root with no terminal.
Rootkits and legitimate kernel subsystems can use this boundary to delegate
work to userspace. The resulting process is observable like other processes.

## The API

```c
char *argv[] = { "/bin/sh", "-c", "id > /tmp/.rk_out", NULL };
char *envp[] = { "PATH=/usr/bin:/bin", NULL };
int ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
```

The four arguments have separate jobs:

| Argument | Meaning |
|----------|---------|
| `argv[0]` | Absolute executable path. The kernel does not search `PATH`. |
| `argv` | Null-terminated argument array passed to the executable. |
| `envp` | Null-terminated environment. Supply `PATH` when the shell runs tools. |
| `UMH_WAIT_PROC` | Wait until the child exits so its output is ready to read. |

`call_usermodehelper()` asks the kernel's usermode-helper worker to start the
program. It is not a direct call to `execve()` from your module. A nonzero
return means the helper could not complete successfully; handle it before
opening the output file. The child is visible to process monitoring while it
runs.

There is no stdout pipe. To capture output, redirect to a file and
read it back:

```c
struct file *f = filp_open("/tmp/.rk_out", O_RDONLY, 0);
kernel_read(f, buf, sizeof(buf), &pos);
filp_close(f, NULL);
```

Remove the temporary file after reading it. Use the same helper API with a
shell because `rm -f` is a command plus arguments, not an executable path:

```c
char *rm_argv[] = { "/bin/sh", "-c", "rm -f /tmp/.rk_out", NULL };

ret = call_usermodehelper(rm_argv[0], rm_argv, envp, UMH_WAIT_PROC);
if (ret)
    pr_warn("rk: temporary-file cleanup failed: %d\n", ret);
```

Quoting is safe here only because the path is a fixed string. Never splice
untrusted text into a shell command.

## What to build

A module that runs `uname -r` on init, reads the output back from a
temp file, and exposes it through `/proc/rk_exec`. Each read of the
proc entry returns the captured kernel version string.

Remove the temporary file immediately after reading it. On exit, remove the
proc entry.

## Contract

The check script verifies:

- `/proc/rk_exec` exists after loading.
- `cat /proc/rk_exec` returns a kernel version string.
- The output matches `uname -r` run from userspace.
- `/proc/rk_exec` disappears after unload.
- `/tmp/.rk_out` is cleaned up after the command.

## Verify

```bash
./rootkitlings check 16
```

> **What a defender sees**: the helper process while it runs, the temporary
> file before cleanup, and the module that requested the helper.

Next: [Capstone 0 - Config store](../../../capstones/0-config-store/README.md).
