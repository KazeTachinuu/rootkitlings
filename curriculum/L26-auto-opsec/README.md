# Auto-hide the agent's own traces

Every previous hiding operation required an explicit command from the operator.
A real rootkit cannot wait for someone to type "HIDE PORT 4444" after
connecting: by then, the port is already visible in `/proc/net/tcp` and the
agent kthread shows in `ps`.
Add automatic OPSEC: on connect, the agent hides its own traces; on disconnect,
it removes them.

## What to hide on connect

Two things betray a connected agent:

1. The kthread PID (visible in `ps aux` as a kworker impostor)
2. The TCP source port (visible in `ss -tlnp` and `/proc/net/tcp`)

Both must be added to the existing target arrays the moment the callback
connection is established, before any operator command is processed.

## Getting the source port

After `rk_connect()` returns the outbound socket, the TCP layer has assigned a
local ephemeral source port.
Extract it from the inet socket structure:

```c
#include <net/inet_sock.h>

unsigned short sport = ntohs(inet_sk(sock->sk)->inet_sport);
```

Convert this to a string with `snprintf` and add it to `port_targets`.

## Getting the kthread PID

The agent kthread's PID is available from `current->pid` inside the kthread
function.
Convert to a string and add to `pid_targets`.

## Connect-time auto-hide

```text
rk_connect() returns sock
        |
        v
  handshake completes
        |
        v
  get source port from inet_sk(sock->sk)->inet_sport
  get kthread PID from current->pid
        |
        v
  add port string to port_targets
  add PID string to pid_targets
        |
        v
  rk_handle_session(&sess)   <-- agent is invisible during session
        |
        v
  session ends
        |
        v
  remove port from port_targets
  remove PID from pid_targets
```

## Removing entries on disconnect

On disconnect, find and remove the auto-added entries from the target arrays.
Match by value (the PID and port strings you added), then call
`rk_target_remove` to shift entries down and decrement the count.

## Interaction with manual commands

The operator can still manually HIDE and UNHIDE targets during a session.
The auto-hide entries coexist in the same arrays.
On disconnect, the cleanup searches for the exact values that were auto-added
and removes only those.

## Observing the effect

Before (no auto-OPSEC):

```text
vm$ ps aux | grep kworker/u8:3
root  1234  ...  kworker/u8:3          <-- visible

vm$ ss -tnp | grep <source-port>
ESTAB  ...  127.0.0.1:<port>  ...      <-- visible
```

After, while the agent is connected:

```text
vm$ ps aux | grep kworker/u8:3         <-- gone
vm$ ss -tnp | grep <source-port>       <-- gone
```

On disconnect, the entries are removed and the process/port reappear (unless the
operator hid them manually).

## Your task

Edit `core/opsec.c`:

1. Implement `rk_auto_hide(struct socket *sock)`: extract the source port with
   `inet_sk(sock->sk)->inet_sport`, get the kthread PID from `current->pid`, add
   both to their target arrays.
2. Implement `rk_auto_unhide()`: find and remove the auto-added PID and port
   entries by value.

Edit `network/agent.c`:

3. Call `rk_auto_hide(sock)` after the handshake succeeds but before the session
   loop.
4. Call `rk_auto_unhide()` after the session loop returns but before cleanup.

Edit `include/rootkit.h`:

5. Add prototypes for `rk_auto_hide` and `rk_auto_unhide`.

Run:

```sh
./rklings check
```

## References

- [`inet_sk` and `inet_sport`][inet]
- [`current` macro][sched]
- [`ntohs`][byteorder]

[inet]: https://github.com/torvalds/linux/blob/v7.2/include/net/inet_sock.h
[sched]: https://github.com/torvalds/linux/blob/v7.2/include/linux/sched.h
[byteorder]: https://github.com/torvalds/linux/blob/v7.2/include/linux/byteorder
