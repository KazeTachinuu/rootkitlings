# M06: Content and port hiding

This milestone builds the remaining "hide" capabilities: hiding lines
from file reads (content hiding) and hiding TCP ports from /proc/net/tcp
(port hiding). Combined with M03's directory/PID hiding, this gives the
rootkit a complete concealment toolkit.

```text
file read:                        /proc/net/tcp listing:

  vfs_read returns                  tcp4_seq_show called
       |                                  |
       v                                  v
  copy buf to kernel              check source port
       |                          against port list
       v                                  |
  scan lines for                   +------+------+
  target strings                   |             |
       |                         match        no match
  +----+----+                      |             |
  |         |                   return 1      return 0
match    no match               (skip line)   (show line)
  |         |
remove    keep
  |         |
  +----+----+
       |
  copy back, adjust
  return value
```

## Content hiding with vfs_read

A kretprobe on `vfs_read` intercepts every file read. In the return
handler, copy the user buffer into a kernel scratch buffer, scan it
line by line, and remove any line containing a target string. Copy the
shortened buffer back and adjust the return value.

This is the same line-removal technique from L15, but now applied to
a dynamic target list instead of a hardcoded string. Any string can be
hidden at runtime via the proc interface.

## Port hiding with tcp4_seq_show

A kprobe pre-handler on `tcp4_seq_show` checks whether the socket's
source port is in the port target list. If it matches, the handler
returns 1 to skip the probed function entirely, so that port's line
never appears in /proc/net/tcp.

This is the same approach from L26, extended from a single module
parameter to a dynamic target list.

## Dynamic target lists

Both hiding mechanisms use the same `rk_target_list` structure from M02.
Content targets use substring matching (does the line contain the
target string?), while port targets use number matching (does the port
equal a stored value?). The proc interface accepts:

| Command | Effect |
|---------|--------|
| `hide content <string>` | Add string to content target list |
| `hide port <N>` | Add port number to port target list |
| `unhide content <N>` | Remove content target at index N |
| `unhide port <N>` | Remove port target at index N |

Reading /proc/rklings shows both target lists with their indices.

## Your task

In `main.c`, implement:
1. `rk_remove_matching_lines()` - line-by-line buffer scan with target list matching
2. `rk_read_return()` - vfs_read kretprobe return handler
3. `rk_tcp_show_pre()` - tcp4_seq_show kprobe pre-handler
4. `rk_write()` - proc command parser for hide/unhide content/port
5. `rk_init()` / `rk_exit()` - module lifecycle

Run:

```sh
./rklings check
```

## References

- [Linux v7.2 `vfs_read` and file operations][1]
- [Linux v7.2 `tcp4_seq_show`][2]
- [Kprobes API][3]

[1]: https://github.com/torvalds/linux/blob/v7.2/fs/read_write.c
[2]: https://github.com/torvalds/linux/blob/v7.2/net/ipv4/tcp_ipv4.c
[3]: https://docs.kernel.org/7.2/trace/kprobes.html
