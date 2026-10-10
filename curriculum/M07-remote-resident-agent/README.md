# Call home and stage a reversible load request

The resident module can run and conceal one worker, but the operator still needs
a local procfs write.
Give it a bounded call-home channel and stage one reversible `modules-load.d`
request in the disposable guest.

This exercise proves the request's exact bytes, guarded replacement, and
rollback on failure or unload.
It does not install the module, update module dependencies, reboot the guest, or
prove that a later boot loads anything.

This project adds two behaviors:

- call one control point, exchange a small command, and reconnect if the point
  is unavailable;
- stage one `modules-load.d` request without destroying a file owned by someone
  else.

`peer_address=` selects one IPv4 control point and defaults to `127.0.0.1`.
Reject a malformed address before acquiring any module resource.
M08 later protects this channel with authenticated encryption.

The agent does not accept shell commands.
Its complete command set is:

- `PING` returns `PONG`;
- `INFO` returns `INFO rootkitlings`;
- `MARK label` records a 1-to-16-byte label and returns it;
- `QUIT` closes the current session.

A marker contains 1 to 16 ASCII letters, digits, `-`, or `_`.
Anything else returns `ERR` without changing it.
Use the two-byte big-endian framing reused from M05 and its exact partial-I/O
and oversized-frame behavior.

## Call home, retry, and stop

The worker is the only socket owner.
For each attempt it:

1. creates one `init_net` IPv4 TCP socket;
2. publishes the socket under the agent lock before blocking in `connect`;
3. connects to the validated IPv4 address and configured port;
4. handles one framed session;
5. shuts down and releases the socket;
6. waits before the next attempt.

Retry starts at `retry_base_ms`, doubles after transport failure up to
`retry_max_ms`, and resets after connection.
Both are nonzero, base does not exceed maximum, and maximum is 5000 ms.

Under the publication lock, stop sets its gate and shuts down an active socket.
It then wakes retry and joins; the worker performs the matching
`sock_release()`.

## Stage a reversible load request

The request file contains exactly:

```text
rootkitlings
```

In a private guest `tmpfs`, the supplied transaction replaces only
`rootkitlings.conf`, retains the original under its fixed sibling name, and
restores only while both names still match retained identity, metadata, and
bytes.
Foreign change suppresses rollback.

`modules-load.d` is only a request consumed by boot tooling.
The course VM has no persistent guest disk, and this checker performs one boot.
The transaction therefore proves safe staging and rollback, not persistence.

## Your task

Edit:

- `Kbuild`
- `core/main.c`
- `network/agent.c`

Keep the supplied authorization, telemetry, evidence view, command parser, and
status endpoint unchanged.
M07 imports the exact framing from M05 and guarded file transaction from M06.

In `Kbuild`, add the supplied components to the cumulative module.
In `network/agent.c`, implement the worker, retry policy, fixed command
dispatch, socket ownership, stop, and snapshot functions declared in
`include/agent.h`.
In `core/main.c`, add the agent and load request to one acquisition and
reverse-release graph.

The added resources are acquired after the inherited process-view hooks.
The public endpoints are published only after the transaction and agent are
ready.
On failure or unload, remove the endpoints first, stop the agent, restore and
dispose the transaction, release its platform state, then continue the inherited
teardown.

Run:

```sh
./rootkitlings check
```

The checker covers commands, retry after refusal, two sessions, blocked-receive
unload, inherited behavior, the exact load request and rollback, and guest logs.

## References

- Kernel socket operations and `sock_release()`
  <https://github.com/torvalds/linux/blob/v7.2/include/linux/net.h>
- `kernel_connect()`, `kernel_sendmsg()`, `kernel_recvmsg()`, and shutdown
  <https://github.com/torvalds/linux/blob/v7.2/net/socket.c>
- Kernel threads and wait queues
  <https://docs.kernel.org/7.2/driver-api/basics.html>
- `modules-load.d` file format
  <https://www.freedesktop.org/software/systemd/man/latest/modules-load.d.html>
- VFS directory-locking rules
  <https://docs.kernel.org/filesystems/directory-locking.html>
