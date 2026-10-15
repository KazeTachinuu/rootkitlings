# Auto-hide the agent's own traces

Every previous hiding operation required an explicit command from the operator.
A real rootkit cannot wait for someone to type "HIDE PORT 4444" after connecting:
by then, the port is already visible in `/proc/net/tcp` and the agent kthread
shows in `ps`. Add automatic OPSEC: on connect, the agent hides its
own traces; on disconnect, it removes them.

## What to hide on connect

Two things betray a connected agent:

1. The kthread PID (visible in `ps aux` as a kworker impostor)
2. The TCP source port (visible in `ss -tlnp` and `/proc/net/tcp`)

Both must be added to the existing target arrays the moment a client connects,
before any operator command is processed.

## Getting the source port

After `kernel_accept` returns the client socket, the TCP layer has assigned an
ephemeral source port. Extract it from the inet socket structure:

```c
#include <net/inet_sock.h>

unsigned short sport = ntohs(inet_sk(client->sk)->inet_sport);
```

Convert this to a string with `snprintf` and add it to `port_targets`.

## Getting the kthread PID

The agent kthread's PID is available from `current->pid` inside the kthread
function, or from `rk_thread->pid` if you stored the task_struct pointer.
Convert to a string and add to `pid_targets`.

## Connect-time auto-hide

```text
kernel_accept returns client
        |
        v
  get source port from inet_sk(client->sk)->inet_sport
  get kthread PID from current->pid
        |
        v
  add port string to port_targets
  add PID string to pid_targets
        |
        v
  rk_handle_client(client)   <-- agent is invisible during session
        |
        v
  client disconnects
        |
        v
  remove port from port_targets
  remove PID from pid_targets
```

## Removing entries on disconnect

Track the indices at which the auto-added entries were inserted. On disconnect,
remove them by shifting later entries down:

```c
for (int j = idx; j < count - 1; j++)
    targets[j] = targets[j + 1];
count--;
```

Remove in reverse order of insertion (higher index first) so the shift does not
invalidate the other tracked index.

## Interaction with manual commands

The operator can still manually HIDE and UNHIDE targets during a session. The
auto-hide entries coexist in the same arrays. When the operator issues
UNHIDE ALL, reset the auto-hide indices to -1 so the disconnect cleanup does
not corrupt an empty array.

## Observing the effect

Before:

```text
vm$ ps aux | grep kworker/u8:3
root  1234  ...  kworker/u8:3          <-- visible

vm$ ss -tlnp | grep 4444
LISTEN  0  1  127.0.0.1:4444  ...      <-- visible
```

After, while the agent is connected:

```text
vm$ ps aux | grep kworker/u8:3         <-- gone
vm$ ss -tlnp | grep 4444               <-- gone
```

On disconnect, the entries are removed and the process/port reappear (unless
the operator hid them manually).

## Your task

Edit `main.c`:

1. Add tracking variables for auto-hidden port and PID indices (initialized
   to -1).
2. Implement `rk_auto_hide`: after `kernel_accept`, extract the source port
   with `inet_sk`, get the kthread PID, add both to their target arrays,
   record the insertion indices.
3. Implement `rk_auto_unhide`: remove the auto-added entries by index (higher
   index first), reset tracking to -1.
4. Call `rk_auto_hide` immediately after `kernel_accept` succeeds.
5. Call `rk_auto_unhide` after `rk_handle_client` returns.
6. Reset auto-hide indices in `rk_cmd_unhide_all`.

Run:

```sh
./rklings check
```

## References

- [`inet_sk` and `inet_sport`](https://github.com/torvalds/linux/blob/v7.2/include/net/inet_sock.h)
- [`current` macro](https://github.com/torvalds/linux/blob/v7.2/include/linux/sched.h)
- [`ntohs`](https://github.com/torvalds/linux/blob/v7.2/include/linux/byteorder/generic.h)
