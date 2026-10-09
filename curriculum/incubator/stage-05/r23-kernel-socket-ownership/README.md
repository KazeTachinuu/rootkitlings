# Own a kernel TCP socket from creation through release

This exercise opens one IPv4 TCP connection from the kernel to a loopback listener supplied by the checker.
It does not send or receive application data.
The mechanism here is ownership: create one endpoint, connect it, record what the kernel assigned, and release it on every later path.

## The lifetime

`sock_create_kern()` creates a socket in the network namespace passed to it.
This exercise accepts only `init_net`, even when the process loading the module is in another network namespace.
It also accepts only `AF_INET`, `SOCK_STREAM`, `IPPROTO_TCP`, blocking connect flags, a nonzero port, and `127.0.0.1`.

Keep the new socket in a local variable while it is incomplete:

1. Create it with `sock_create_kern()`.
2. Connect it with `kernel_connect(..., 0)`.
3. Read the local and peer endpoints with `kernel_getsockname()` and `kernel_getpeername()`.
4. Require both calls to return exactly one `struct sockaddr_in` and validate their family, address, and ports.
5. Store the captured configuration and endpoints.
6. Publish `owner->socket` last.

Return the exact negative error from create, connect, or either name query.
A successful name query with the wrong shape or endpoint is `-EPROTO`.
The checker injects `-ECANCELED` after creation and after endpoint capture to verify both cleanup paths.

Creation failure owns nothing and releases nothing.
Any failure after successful creation releases the socket.
Any failure after successful connection first attempts `kernel_sock_shutdown(..., SHUT_RDWR)`, then releases the socket regardless of the shutdown result.
Keep the original failure code.

Shutdown changes connection state; it does not free the socket.
`sock_release()` ends ownership.

## Closing and reporting

Closing a null or empty owner does nothing.
For an owned socket, clear the published pointer and recorded state before shutdown and release.
The supplied module removes `/proc/rootkitlings` before it closes the socket.

`rk_socket_snapshot()` must copy a consistent view while proc readers may run concurrently.
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

The addresses and ports must come from the two name queries.
`state=owned` describes this module's resource ownership, not the current TCP state.

These operations may sleep.
This lesson runs them only during module initialization and exit, where blocking is allowed.
Do not add a worker, timer, packet hook, probe, nonblocking state machine, bind, listen, accept, send, or receive path.

## Your task

Edit one file:

- `network/socket_owner.c`

Implement `rk_socket_open()`, `rk_socket_close()`, and `rk_socket_snapshot()`.

Run:

```bash
./rootkitlings check
```

The checker accepts equivalent implementations of the required lifetime.
It builds with `W=1`, checks exact errors and rollback, loads from a child network namespace, matches the captured connection in `init_net`, keeps an unrelated connection as a control, reads status concurrently, unloads with the peer held open, checks prompt EOF, reloads, and checks the kernel log.

## Linux v7.2 references

- [`sock_create_kern()`, the kernel socket operations, and `sock_release()` declarations](https://github.com/torvalds/linux/blob/v7.2/include/linux/net.h)
- [`sock_create_kern()`, `kernel_connect()`, name queries, shutdown, and release implementations](https://github.com/torvalds/linux/blob/v7.2/net/socket.c)
- [IPv4 socket operations](https://github.com/torvalds/linux/blob/v7.2/net/ipv4/af_inet.c)
- [Initial network namespace declaration](https://github.com/torvalds/linux/blob/v7.2/include/net/net_namespace.h)
