# Open and close a kernel call-home socket

Build a small call-home socket owner for one IPv4 TCP connection to the
checker's loopback listener.
Open it, verify both endpoints, publish it only when complete, and close it
exactly once on every path.

No application data moves yet.
Success proves that the kernel socket has one clear owner from creation through
failure, publication, and release.

## The lifetime

`sock_create_kern()` creates a socket in the network namespace passed to it.
This exercise accepts only `init_net`, even when the process loading the module
is in another network namespace.
It also accepts only `AF_INET`, `SOCK_STREAM`, `IPPROTO_TCP`, blocking connect
flags, a nonzero port, and `127.0.0.1`.

Keep the new socket in a local variable while it is incomplete:

1. Create it with `sock_create_kern()`.
2. Connect it with `kernel_connect(..., 0)`.
3. Read the local and peer endpoints with `kernel_getsockname()` and
   `kernel_getpeername()`.
4. Require both calls to return exactly one `struct sockaddr_in` and validate
   their family, address, and ports.
5. Store the captured configuration and endpoints.
6. Publish `owner->socket` last.

Return the exact negative error from create, connect, or either name query.
A successful name query with the wrong shape or endpoint is `-EPROTO`.
The checker injects `-ECANCELED` after creation and after endpoint capture to
verify both cleanup paths.

Creation failure owns nothing and releases nothing.
Any failure after successful creation releases the socket.
Any failure after successful connection first attempts
`kernel_sock_shutdown(..., SHUT_RDWR)`, then releases the socket regardless of
the shutdown result.
Keep the original failure code.

Shutdown changes state; only `sock_release()` ends ownership.
TCP may retain `TIME_WAIT`; the captured tuple need only leave `ESTABLISHED`.

## Closing and reporting

Closing a null or empty owner does nothing.
For an owned socket, clear the published pointer and recorded state before
shutdown and release.
The supplied module removes `/proc/rootkitlings` before it closes the socket.

`rk_socket_snapshot()` must copy a consistent view while proc readers may run
concurrently.
Use the supplied mutex for open, close, and snapshot.
The proc record is exactly:

```text
state=owned
namespace=init_net
family=ipv4
type=stream
protocol=tcp
connect_flags=0
connect_result=0
local_address=127.0.0.1
local_port=N
peer_address=127.0.0.1
peer_port=N
```

Report queried addresses and ports.
`state=owned` means module ownership, not TCP state.

These operations may sleep.
This lesson runs them only during module initialization and exit, where blocking
is allowed.
Do not add a worker, timer, packet hook, probe, nonblocking state machine, bind,
listen, accept, send, or receive path.

## Your task

Edit one file:

- `network/socket_owner.c`

Implement `rk_socket_open()`, `rk_socket_close()`, and `rk_socket_snapshot()`.

Run:

```sh
./rootkitlings check
```

The checker uses `W=1` and verifies exact errors, rollback, and `init_net`
ownership.
It also tests independent controls, concurrent status, unload, and reload.

## References

- [`sock_create_kern()`, the kernel socket operations, and `sock_release()`
  declarations][1]
- [`sock_create_kern()`, `kernel_connect()`, name queries, shutdown, and release
  implementations][2]
- [IPv4 socket operations][3]
- [Initial network namespace declaration][4]

[1]: https://github.com/torvalds/linux/blob/v7.2/include/linux/net.h
[2]: https://github.com/torvalds/linux/blob/v7.2/net/socket.c
[3]: https://github.com/torvalds/linux/blob/v7.2/net/ipv4/af_inet.c
[4]: https://github.com/torvalds/linux/blob/v7.2/include/net/net_namespace.h
