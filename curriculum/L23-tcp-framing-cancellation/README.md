# Run a stoppable call-home session

The call-home socket now carries binary messages.
Its peer may split a message across reads, send an oversized frame, or remain
blocked while the module unloads.

Echo valid frames, drain oversized ones, and guarantee that stop wakes and joins
the worker.
Success proves that one kernel thread can own a framed TCP session without
leaking a blocked socket.

TCP is a byte stream, and successful socket calls may transfer short.
Add framing while one kernel thread owns every blocking socket operation.

## Frame format

Each frame is a two-byte big-endian payload length followed by that many payload
bytes.
Lengths from 0 through 64 are valid, including empty frames and arbitrary binary
data.

Send and receive each use separate exact loops for header and payload.
Advance only after a positive return, return a negative socket error unchanged,
treat a zero send as `-EPIPE`, and treat a zero receive as `-ECONNRESET`.
Reject a positive result larger than the remaining request with `-EPROTO`.

A declared length from 65 through 65535 is `-EMSGSIZE`, but only after all
declared payload bytes have been received and discarded through a fixed buffer
no larger than 64 bytes.
The session counts that frame and continues, so the following frame still begins
at its own two-byte header.

Echo each accepted binary frame exactly once; do not parse commands or text.

## Socket ownership and stop

The worker is the only code that releases the socket.
After creating a socket, it locks the session, checks `stopping`, and either
releases the unpublished socket or publishes it as `active`.
Publication happens before blocking connect or framed I/O.

Stop follows this order:

1. Lock the session and set `stopping` before inspecting `active`.
2. If a socket is active, start `SHUT_RDWR` while still holding the lock.
3. Wake transport-specific non-socket waits and the session wait queue.
4. Call `kthread_stop()` to stop and join the worker.

This closes both sides of the race.
Stop cannot miss a socket published after it observed null, and the worker
cannot publish a newly created socket after stop has set the gate.
Shutdown unblocks connect or I/O; it does not release the socket.

The module opens one `init_net` IPv4 TCP connection to a dynamic loopback
listener, without retry, authentication, DNS, commands, or persistence.
A zero-initialized session object may be started once at a time; a repeated
start while its task is still owned returns `-EBUSY`.
After stop has joined that task, the object may be started again.
The caller serializes start and stop calls; status snapshots may run while the
worker is active.

`frames` counts frames whose complete echo was sent successfully.
`oversized` counts oversized frames only after their complete declared payload
was drained.
`last_error` is the terminal worker error, and stop-induced errors are not
reported as session failures.
Calling stop again after the worker has been joined has no effect.

## Your task

Edit one file:

- `session/tcp_session.c`

Implement the exact send and receive loops, oversized-frame drain, worker,
start, stop, and snapshot functions declared in `include/rootkit.h`.

Run:

```bash
./rootkitlings check
```

Instrumented transports cover partial, zero, negative, and impossible returns;
binary framing; oversized drain; cancellation in every blocking phase; the
publish/stop race; idempotent stop; and exactly one release.
The Linux guest verifies loopback, blocked-receive unload, rollback, and reload.

## Linux v7.2 references

- [Kernel socket operations and `sock_release()`][1]
- [`kernel_sendmsg()`, `kernel_recvmsg()`, connect, shutdown, and release][2]
- [Kernel thread creation and stopping][3]
- [Wait queues and wake events][4]

[1]: https://github.com/torvalds/linux/blob/v7.2/include/linux/net.h
[2]: https://github.com/torvalds/linux/blob/v7.2/net/socket.c
[3]: https://docs.kernel.org/7.2/driver-api/basics.html#kernel-threads
[4]:
  https://docs.kernel.org/7.2/driver-api/basics.html#wait-queues-and-wake-events
