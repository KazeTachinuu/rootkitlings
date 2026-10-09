# Run one cancellable framed TCP session

TCP carries a byte stream, not messages.
One `kernel_sendmsg()` or `kernel_recvmsg()` call may transfer fewer bytes than requested even when it succeeds.
This exercise adds an explicit frame boundary and keeps every blocking socket operation owned by one kernel thread.

## Frame format

Each frame is a two-byte big-endian payload length followed by that many payload bytes.
Lengths from 0 through 64 are valid, including empty frames and arbitrary binary data.

Sending a frame requires two exact loops: one for the header and one for the payload.
Receiving requires the same separation.
Advance only after a positive return, return a negative socket error unchanged, treat a zero send as `-EPIPE`, and treat a zero receive as `-ECONNRESET`.
Reject a positive result larger than the remaining request with `-EPROTO`.

A declared length from 65 through 65535 is `-EMSGSIZE`, but only after all declared payload bytes have been received and discarded through a fixed buffer no larger than 64 bytes.
The session counts that frame and continues, so the following frame still begins at its own two-byte header.

The supplied handler echoes each accepted frame exactly once.
It does not parse commands or text.

## Socket ownership and stop

The worker is the only code that releases the socket.
After creating a socket, it locks the session, checks `stopping`, and either releases the unpublished socket or publishes it as `active`.
Publication happens before blocking connect or framed I/O.

Stop follows this order:

1. Lock the session and set `stopping` before inspecting `active`.
2. If a socket is active, start `SHUT_RDWR` while still holding the lock.
3. Wake transport-specific non-socket waits and the session wait queue.
4. Call `kthread_stop()` to stop and join the worker.

This closes both sides of the race.
Stop cannot miss a socket published after it observed null, and the worker cannot publish a newly created socket after stop has set the gate.
Shutdown unblocks connect or I/O; it does not release the socket.

The supplied module opens one `init_net` IPv4 TCP connection to a dynamic loopback listener.
It does not reconnect, retry, authenticate, resolve DNS, execute commands, or persist anything.
A zero-initialized session object may be started once at a time; a repeated start while its task is still owned returns `-EBUSY`.
After stop has joined that task, the object may be started again.
The caller serializes start and stop calls; status snapshots may run while the worker is active.

The status counters have narrow meanings.
`frames` counts frames whose complete echo was sent successfully.
`oversized` counts oversized frames only after their complete declared payload was drained.
`last_error` is the terminal worker error, and stop-induced errors are not reported as session failures.
Calling stop again after the worker has been joined has no effect.

## Your task

Edit one file:

- `session/tcp_session.c`

Implement the exact send and receive loops, oversized-frame drain, worker, start, stop, and snapshot functions declared in `include/rootkit.h`.

Run:

```bash
./rootkitlings check
```

The checker accepts structurally different implementations that satisfy the behavior.
Instrumented transports cover every positive partial boundary; zero, negative, and impossible returns in each phase; empty and binary frames; split and coalesced input; both oversized extremes followed by a valid frame; blocked connect, header, payload, send, and drain cancellation; spontaneous create, connect, and EOF failures; the create/publish/stop race; idempotent stop; and exactly one release.
The disposable Linux 7.2 guest then verifies a real loopback session, unload while blocked in receive, rollback, reload, and clean logs.

## Linux v7.2 references

- [Kernel socket operations and `sock_release()`](https://github.com/torvalds/linux/blob/v7.2/include/linux/net.h)
- [`kernel_sendmsg()`, `kernel_recvmsg()`, connect, shutdown, and release](https://github.com/torvalds/linux/blob/v7.2/net/socket.c)
- [Kernel thread creation and stopping](https://docs.kernel.org/7.2/driver-api/basics.html#kernel-threads)
- [Wait queues and wake events](https://docs.kernel.org/7.2/driver-api/basics.html#wait-queues-and-wake-events)
