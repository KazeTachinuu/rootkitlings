# Auto-hide the agent's own traces

Every previous hiding operation required an explicit command from the operator.
A real rootkit cannot wait for someone to type "HIDE PORT 4444" after connecting:
by then, the port is already visible in `/proc/net/tcp` and the agent kthread
shows in `ps`. This lesson adds automatic OPSEC: on connect, the agent hides its
own traces; on disconnect, it removes them.

## Connect-time hiding

When a client connects and the agent thread begins handling it, the module
should automatically add the control port to the hidden ports list. When the
client disconnects, the module removes the entry it added.

```text
client connects
       |
       v
  kernel_accept returns
       |
       v
  rk_auto_hide()
       |
       +-- record current port_count as auto_port_index
       +-- add RK_PORT to hidden_ports[port_count]
       +-- increment port_count
       |
       v
  rk_handle_client(client)
       |
       v
  client disconnects (recv returns <= 0)
       |
       v
  rk_auto_unhide()
       |
       +-- shift hidden_ports entries from auto_port_index forward
       +-- decrement port_count
       +-- reset auto_port_index to -1
```

## Removing by index

When the agent disconnects, its auto-hidden entry must be removed from the
target array. Track the index at which the entry was added. To remove entry
at index `i`, shift all later entries down by one:

```c
for (int j = i; j < count - 1; j++)
    array[j] = array[j + 1];
count--;
```

This preserves the order of any entries the operator added manually.

## Interaction with UNHIDE ALL

The `UNHIDE ALL` command resets all target counts to zero. When that happens, the
auto-hidden entry is gone too. Reset `auto_port_index` to `-1` so that
`rk_auto_unhide` does not try to shift a now-empty array.

## Observing the effect

Before this lesson, `ss -tlnp` or `cat /proc/net/tcp` always shows the control
port. With auto-opsec, the port disappears the moment the agent accepts a
client, and reappears when the client disconnects.

## Your task

Edit `main.c`:

1. Declare `auto_port_index` initialized to `-1`.
2. Implement `rk_auto_hide`: check capacity, record the index, add `RK_PORT`
   to `hidden_ports`, increment `port_count`.
3. Implement `rk_auto_unhide`: validate the index, shift entries down, decrement
   `port_count`, reset the index.
4. Call `rk_auto_hide` in `rk_agent_thread` immediately after `kernel_accept`
   succeeds.
5. Call `rk_auto_unhide` after `rk_handle_client` returns.
6. Reset `auto_port_index` to `-1` in `rk_cmd_unhide_all`.

Run:

```sh
./rklings check
```

## References

- [`current` macro and task_struct](https://github.com/torvalds/linux/blob/v7.2/include/linux/sched.h)
- [Process information in /proc](https://docs.kernel.org/7.2/filesystems/proc.html)
