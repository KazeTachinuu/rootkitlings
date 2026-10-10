# Hand a loopback socket to a root shell

A kernel thread cannot become an interactive userspace shell.
It has no userspace address space, executable image, or file-descriptor table.

Build the boundary instead:

```text
loopback listener <-> kernel TCP socket <-> stdin, stdout, stderr <-> /bin/sh
```

The module connects only to `127.0.0.1` in the disposable guest.
It starts one shell, stops it after a fixed timeout, and leaves no socket,
process, work item, or reference behind on unload.

## The handoff

Create and connect one kernel socket from a work item.
`sock_alloc_file()` gives that socket normal file semantics.

Use a user-mode-helper setup callback to install three references to that file
as descriptors 0, 1, and 2 in the helper's clean descriptor table.
Then execute `/bin/sh` with `call_usermodehelper_exec()` and wait for the
process to finish.

The helper API creates the userspace process with root credentials.
Do not edit credentials in this exercise.

The helper starts a new session before the final shell:

```sh
/bin/setsid /bin/sh
```

The process keeps the same PID and becomes its process-group leader.
That lets timeout and unload terminate the shell and commands it started.
No learner-controlled text reaches `sh -c`.

## One owned session

`rk_root_shell_request()` admits one live request at a time.
It rejects a concurrent request, a zero port, or a stopped component without
changing live state.
A completed, refused, or timed-out request leaves the component ready for
another port.

The work item owns every blocking operation:

1. create and publish the socket;
2. arm the timeout;
3. connect to the fixed loopback address;
4. convert the socket into a file;
5. run and wait for the helper;
6. cancel the timeout and release every reference.

The timeout and stop paths set their gate under the mutex, shut down the
published socket, and send `SIGKILL` to both the process group and its leader.
They never release an object still owned by the worker.

`rk_root_shell_stop()` cancels the timeout, waits for the worker, then clears
the component.
It is safe before a request, during connect, while the shell is reading, while a
command is running, and after the shell exits.

## Your task

Edit:

- `execution/root_shell.c`

Implement initialization, one-session admission, the socket-to-file handoff, the
user-mode-helper setup, timeout, worker cleanup, and stop path declared in
`include/root_shell.h`.

Run:

```sh
./rootkitlings check
```

The checker opens a real loopback shell, proves its UID is zero, forces its
timeout, unloads it while blocked, exercises a refused connection, reloads the
module, and checks the kernel log.

## References

- Linux v7.2 user-mode helpers
  <https://github.com/torvalds/linux/blob/v7.2/kernel/umh.c>
- Linux v7.2 user-mode-helper interface
  <https://github.com/torvalds/linux/blob/v7.2/include/linux/umh.h>
- Socket-to-file ownership
  <https://github.com/torvalds/linux/blob/v7.2/net/socket.c>
- Descriptor allocation and installation
  <https://github.com/torvalds/linux/blob/v7.2/fs/file.c>
- Process-group signaling
  <https://github.com/torvalds/linux/blob/v7.2/kernel/signal.c>
