# 16 - Run a userspace helper

## Crossing the boundary

Kernel code should not reimplement ordinary userspace tools.

`call_usermodehelper()` asks the kernel to start one explicit program with one explicit argument vector.

This is useful when a kernel event must hand work to a userspace program, such as loading firmware or notifying a service.
A malicious module can abuse the same mechanism to launch a privileged process.

```text
module load
    |
    |  /bin/touch /tmp/rk_helper_marker
    v
helper exits -----> marker exists

module unload
    |
    |  /bin/rm -f /tmp/rk_helper_marker
    v
helper exits -----> marker is gone
```

## Program and arguments

The executable and its arguments are separate strings:

```c
char *argv[] = { "/bin/rm", "-f", MARKER_PATH, NULL };
```

`argv[0]` is the executable path.
The remaining entries are its arguments.
The final `NULL` terminates the array.

There is no shell here.
`-f` is passed directly to `/bin/rm`, so quotes, redirects, substitutions, and command separators are never interpreted.

The environment is another null-terminated array:

```c
static char *helper_env[] = {
	"HOME=/",
	"PATH=/usr/sbin:/usr/bin:/sbin:/bin",
	NULL,
};
```

An absolute executable path is still required.
The kernel does not use `PATH` to locate `argv[0]`.

## What to implement

Complete `run_helper()` with `call_usermodehelper()`.

Pass `argv[0]`, `argv`, `helper_env`, and `UMH_WAIT_PROC`.
Waiting matters because module initialization must know whether `/bin/touch` succeeded before reporting success.

The supplied lifecycle creates the marker on load and removes it on unload.

## Run it

```bash
./rootkitlings start L16
$EDITOR module/hello.c
./rootkitlings run L16
```

Inside the VM:

```sh
insmod hello.ko
ls -l /tmp/rk_helper_marker
rmmod hello
test ! -e /tmp/rk_helper_marker && echo cleaned
```

Expected result:

```text
-rw-r--r--    1 root     root             0 ... /tmp/rk_helper_marker
cleaned
```

## Verify

```bash
./rootkitlings check L16
```

The check loads the real module, observes the marker created by the helper, unloads the module, and confirms cleanup.

The helper runs as a visible privileged process.
Process and file monitoring can observe both executions and the marker.

Next: [Ling 17 - Timer](../../03_concurrency/17-timer/README.md).

The concurrent configuration store follows ling 23, after mutexes and shared-state lifetime have been introduced.
