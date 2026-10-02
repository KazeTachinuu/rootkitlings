# Ling 16: Usermode exec

Kernel modules can execute userspace programs. `call_usermodehelper`
spawns a process from kernel context, running as root with no terminal.
Every rootkit uses this: Reptile launches its reverse shell, Pumakit
runs operator commands, persistence modules write boot scripts.

## The API

```c
char *argv[] = { "/bin/sh", "-c", "id > /tmp/.rk_out", NULL };
char *envp[] = { "PATH=/usr/bin:/bin", NULL };
int ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
```

`UMH_WAIT_PROC` blocks until the process exits. The return value is
the exit status shifted left by 8. The spawned process is a child of
`kthreadd`, visible in `ps` until it exits.

There is no stdout pipe. To capture output, redirect to a file and
read it back:

```c
struct file *f = filp_open("/tmp/.rk_out", O_RDONLY, 0);
kernel_read(f, buf, sizeof(buf), &pos);
filp_close(f, NULL);
```

## What to build

A module that runs `uname -r` on init, reads the output back from a
temp file, and exposes it through `/proc/rk_exec`. Each read of the
proc entry returns the captured kernel version string.

On exit, remove the proc entry and delete the temp file.

## Contract

The check script verifies:

- `/proc/rk_exec` exists after loading.
- `cat /proc/rk_exec` returns a kernel version string.
- The output matches `uname -r` run from userspace.
- `/proc/rk_exec` disappears after unload.
- `/tmp/.rk_out` is cleaned up after unload.
