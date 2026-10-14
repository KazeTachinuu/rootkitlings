# Content hiding, port hiding, and kernel enumeration

This milestone builds the remaining "hide" capabilities and introduces
kernel-space enumeration: the operator's view of the true system state,
bypassing the rootkit's own hooks.

```text
                   HIDING                              ENUMERATION
          (what userspace sees)                (what the operator sees)

      vfs_read returns buffer                  for_each_process(p)
               |                                       |
               v                                       v
      scan for target strings              task_pid_nr, task_uid, comm
               |                                       |
         +-----+-----+                                 v
         |           |                          seq_printf to proc
       match      no match
         |           |
       remove      keep                    tcp_hashinfo.lhash2[bucket]
         |           |                                 |
         +-----+-----+                                 v
               |                           spin_lock_bh, sk_for_each
      copy back, adjust retval                         |
                                                       v
      tcp4_seq_show called                 inet_sport, inet_rcv_saddr
               |                                       |
               v                                       v
      check port against list               seq_printf to proc
               |
         +-----+-----+
         |           |
       match      no match
         |           |
      rewind       pass
      seq->count   through
```

## Content hiding with vfs_read

A kretprobe on `vfs_read` intercepts every file read.
The entry handler captures the user buffer pointer
(`regs_get_kernel_argument(regs, 1)`).
The return handler does the filtering:

1. Check that the return value is positive and fits the scratch buffer.
   Early-out if `content_targets.count == 0`.
2. `copy_from_user_nofault` the user buffer into a kernel scratch buffer.
3. Walk the scratch buffer line by line (find `\n` with `memchr`).
   For each line, call `rk_target_match(&content_targets, line, len)`.
   Matched lines are skipped; unmatched lines are shifted down with `memmove`.
4. If the total length changed, `copy_to_user_nofault` the filtered buffer back
   and update the return value in `regs_return_value`/ `regs_set_return_value`.

This is the same line-removal technique from L15, applied to a dynamic target
list instead of a hardcoded string.

## Port hiding with tcp4_seq_show

A kretprobe on `tcp4_seq_show` uses entry and return handlers together.

**Entry handler (`rk_tcp_show_entry`):**
1. Initialize `data->seq = NULL`.
2. Read `v` from `regs->si`.
   If `v == SEQ_START_TOKEN`, return 0 (this is the header line, not a socket).
3. Early-out if `port_targets.count == 0`.
4. Cast `v` to `struct sock *`, get `inet_sk()`, read `inet_sport`.
5. If `rk_target_number_match(&port_targets, ntohs(inet->inet_sport))` matches,
   save `seq` (from `regs->di`) and `seq->count` in the per-instance data.

**Return handler (`rk_tcp_show_return`):** If the entry handler saved a seq
pointer, rewind `seq->count` to `data->prev_count`.
This erases the line that `tcp4_seq_show` just wrote, so the port never appears
in `/proc/net/tcp`.

This is the same approach from L26, extended from a single module parameter to a
dynamic target list.

## Kernel-space enumeration

After hiding processes and ports from userspace, the operator needs a way to see
the real system state.
Tools like `ps` and `netstat` read from `/proc`, which passes through the
rootkit's hooks.
They see the filtered view, not the truth.

Kernel-space enumeration bypasses the hooks entirely by reading the kernel's
internal data structures directly:

**Process list.**
`for_each_process()` walks the kernel's doubly linked task list under
`rcu_read_lock`.
Each `task_struct` has the real PID, UID, and command name.
No `/proc` involved, no getdents hook can filter anything.

**Listening ports.**
`tcp_hashinfo.lhash2` is the kernel's hash table of listening TCP sockets.
Walk each bucket under its own `spin_lock_bh`, read `inet_sport` and
`inet_rcv_saddr` from each socket.
No `tcp4_seq_show` hook can suppress anything.

The proc interface exposes both: reading `/proc/rklings` shows the target lists
plus the real process and port tables.
The check script verifies that a hidden port disappears from `netstat` but
remains visible in the proc output.

## Dynamic target lists

Both hiding mechanisms use the same `rk_target_list` structure from M03.
Content targets use substring matching (does the line contain the target
string?), while port targets use number matching (does the port equal a stored
value?).
The proc interface accepts:

| Command | Effect |
|---------|--------|
| `hide content <string>` | Add string to content target list |
| `hide port <N>` | Add port number to port target list |
| `unhide content <N>` | Remove content target at index N |
| `unhide port <N>` | Remove port target at index N |

## Your task

In `main.c`, implement:
1. `rk_remove_matching_lines()` -- line-by-line buffer scan with target list
   matching
2. `rk_read_return()` -- vfs_read kretprobe return handler
3. `rk_tcp_show_entry()` -- tcp4_seq_show kretprobe entry handler
4. `rk_tcp_show_return()` -- tcp4_seq_show kretprobe return handler
5. `rk_show_processes()` -- enumerate all processes via for_each_process
6. `rk_show_ports()` -- enumerate listening TCP ports via tcp_hashinfo
7. `rk_write()` -- proc command parser for hide/unhide content/port
8. `rk_init()` / `rk_exit()` -- module lifecycle

Run:

```sh
./rklings check
```

## References

- [Linux v7.2 `vfs_read`][1]
- [Linux v7.2 `tcp4_seq_show`][2]
- [Linux v7.2 `for_each_process`][3]
- [Kprobes API][4]

[1]: https://github.com/torvalds/linux/blob/v7.2/fs/read_write.c
[2]: https://github.com/torvalds/linux/blob/v7.2/net/ipv4/tcp_ipv4.c
[3]: https://github.com/torvalds/linux/blob/v7.2/include/linux/sched/signal.h
[4]: https://docs.kernel.org/7.2/trace/kprobes.html
