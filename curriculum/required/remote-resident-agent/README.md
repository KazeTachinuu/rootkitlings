# Build the remote resident agent

This milestone extends your module with authorized actions, syscall telemetry, and cross-view evidence by adding two components:

- a kernel thread that connects to one loopback TCP listener, handles one framed session, and reconnects after a bounded delay;
- a guarded modules-load request at `/etc/modules-load.d/rootkitlings.conf`.

The agent does not accept shell commands.
Its complete command set is:

- `PING` returns `PONG`;
- `INFO` returns `INFO rootkitlings`;
- `MARK label` records a 1-to-16-byte label and returns it;
- `QUIT` closes the current session.

A marker may contain only ASCII letters, digits, `-`, and `_`.
Every other command returns `ERR` and changes no marker.
Commands use the supplied two-byte big-endian TCP framing, including its exact partial-I/O and oversized-frame behavior.

## Reconnect and stop

The worker is the only socket owner.
For each attempt it:

1. creates one `init_net` IPv4 TCP socket;
2. publishes the socket under the agent lock before blocking in `connect`;
3. connects only to `127.0.0.1` and the configured port;
4. handles one framed session;
5. shuts down and releases the socket;
6. waits before the next attempt.

The retry delay starts at `retry_base_ms`, doubles after each transport failure, and never exceeds `retry_max_ms`.
A successful connection resets it to the base value.
Both values must be nonzero, the base must not exceed the maximum, and the maximum is 5000 ms.

Stop sets the stop gate while holding the same lock used to publish the socket.
It shuts down an active socket, wakes the retry wait, then joins the worker.
The worker still performs the one matching `sock_release()`.

## Boot request

The request file contains exactly:

```text
rootkitlings
```

The checker mounts a fresh private `tmpfs` directly on `/etc/modules-load.d` inside the disposable guest.
The supplied guarded file transaction replaces only `rootkitlings.conf`, keeps the original under its fixed sibling name, and restores it only while both names still match their retained identities, metadata, and bytes.
A foreign change suppresses rollback instead of being overwritten.

This is a real modules-load request for a later boot.
It does not install `rootkitlings.ko`, update module dependencies, or prove that a reboot loads the module.
The checker verifies the request bytes and transaction ordering, then verifies rollback in the same guest.

## Your work

Edit:

- `Kbuild`
- `core/main.c`
- `network/agent.c`

Keep the supplied authorization, telemetry, evidence-view, framing, command parser, status endpoint, and guarded-transaction files unchanged.

In `Kbuild`, add the supplied components to the cumulative module.
In `network/agent.c`, implement the worker, retry policy, fixed command dispatch, socket ownership, stop, and snapshot functions declared in `include/agent.h`.
In `core/main.c`, add the agent and boot request to one acquisition and reverse-release graph.

The added resources are acquired after the inherited process-view hooks.
The public endpoints are published only after the transaction and agent are ready.
On failure or unload, remove the endpoints first, stop the agent, restore and dispose the transaction, release its platform state, then continue the inherited teardown.

Run:

```bash
./rootkitlings check
```

The checker runs the command and retry model on the host, rejects broken implementations, verifies the complete inherited behavior, forces at least one refused connection before a listener appears, completes two framed sessions, unloads during blocked receive, checks the exact boot request and rollback, and checks the kernel log.

## Linux v7.2 references

- [Kernel socket operations and `sock_release()`](https://github.com/torvalds/linux/blob/v7.2/include/linux/net.h)
- [`kernel_connect()`, `kernel_sendmsg()`, `kernel_recvmsg()`, and shutdown](https://github.com/torvalds/linux/blob/v7.2/net/socket.c)
- [Kernel threads and wait queues](https://docs.kernel.org/7.2/driver-api/basics.html)
- [`modules-load.d` file format](https://www.freedesktop.org/software/systemd/man/latest/modules-load.d.html)
- [VFS directory-locking rules](https://docs.kernel.org/filesystems/directory-locking.html)
