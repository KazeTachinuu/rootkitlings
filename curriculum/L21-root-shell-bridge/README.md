# Hand a TCP socket to a root shell

A kernel thread runs inside the kernel: it has no userspace memory, no program
image, no file descriptors.
It cannot become an interactive shell.

The solution is a **bridge**: the kernel creates a TCP socket and hands it to a
userspace process that runs a real shell.

```text
peer terminal <-> plaintext TCP <-> script PTY <-> interactive root Bash
```

The supplied module parses `peer_address=` as IPv4 and defaults to `127.0.0.1`.
It starts one shell at a time, stops it after a fixed timeout, and leaves no
socket, process, work item, or reference behind on unload.

## The handoff

Create and connect one kernel socket from a work item.
`sock_alloc_file()` wraps that socket in a `struct file`, which is what
userspace needs to use it as a file descriptor.

`call_usermodehelper_exec()` launches a userspace process from the kernel.
Use its setup callback to install the socket file as descriptors 0, 1, and 2
(stdin, stdout, stderr) in the new process.
The fixed helper chain is:

```sh
/bin/setsid /bin/script -q \
  -c '/bin/bash --noprofile --rcfile /etc/rklings.bashrc -i' /dev/null
```

`setsid` gives the helper its own session and process group.
`script` allocates a pseudoterminal (PTY, a virtual terminal device) and
forwards the socket-backed standard descriptors to an interactive Bash process.
The peer therefore gets terminal behavior: command echo, prompts, CRLF line
endings, job-control signals, and terminal escape sequences.
This exercise does not carry terminal-resize metadata, so the shell is intended
for normal command-line interaction rather than full-screen programs.

Execute that fixed chain with `call_usermodehelper_exec()` and wait for it to
finish.

The helper API creates the userspace process with root credentials.
Do not edit credentials in this exercise.

The `setsid` process becomes the session and process-group leader.
Timeout and unload signal that leader and its current process group.
A descendant that deliberately creates another session or process group is
outside this exercise's cleanup boundary.
No learner-controlled text enters the helper argument list.
Interactive commands arrive only through the PTY stream.

Before the handoff, send this exact prelude on the socket:

```text
RKSH <8-hex request id> <16-hex cookie>\n
```

Generate a fresh nonzero cookie for every accepted request.
The peer compares the prelude with the ticket returned by
`rk_root_shell_request()` before it trusts the terminal stream.
This binds one accepted connection to its request; it does not encrypt the
following PTY bytes.

## One owned session

`rk_root_shell_request()` admits one live request at a time.
It rejects a concurrent request, a zero port, or a stopped component without
changing live state.
A completed, refused, or timed-out request leaves the component ready for
another port.
`rk_root_shell_snapshot()` reports the latest request ID, state, and errno.

The work item owns every blocking operation:

1. create and publish the socket;
2. arm the timeout;
3. connect to the requested peer address;
4. send the ownership prelude;
5. convert the socket into a file;
6. run and wait for the helper;
7. publish its final state and release every reference.

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
./rklings check
```

The checker verifies the prelude, proves that Bash has a PTY on descriptors 0,
1, and 2, opens two sequential UID-0 shells, observes refusal and timeout
results, unloads a blocked shell, rejects an invalid peer, and checks the kernel
log.

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
