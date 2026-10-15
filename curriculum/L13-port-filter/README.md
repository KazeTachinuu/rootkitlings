# Hide a listening port from ss/netstat

When `ss -tlnp` lists TCP sockets, the kernel calls `tcp4_seq_show` for each
connection. This function writes one line into a `seq_file` buffer. Intercept
that function and rewind the buffer position when a
matching port is found, effectively erasing the line before userspace sees it.

## How seq_file works

The `/proc/net/tcp` file is backed by `seq_file`. For each socket, the kernel
calls `tcp4_seq_show(struct seq_file *seq, void *v)`. This function formats
one line and appends it to `seq->buf` at position `seq->count`.

The key insight: `seq->count` is the write cursor. If you save its value
before the show function runs and restore it afterward, the line is
effectively deleted. No buffer copying, no memmove.

## The interception pattern

Use kretprobes on `tcp4_seq_show` and `tcp6_seq_show`:

1. **entry_handler**: get the `seq_file` pointer from the first argument
   (`regs->di`). Save `seq->count` in the kretprobe's per-instance data.
   Also save a pointer to the `struct sock` from the second argument
   (`regs->si`). If the second argument is `SEQ_START_TOKEN` (0x1), this is
   the header row; save and skip.

2. **return_handler**: get the `struct sock` saved in entry. Extract the
   source port with `inet_sk(sk)->inet_sport`, convert with `ntohs()`.
   Convert the port number to a string and check against port_targets.
   If it matches: restore `seq->count` to the saved value.

## Required headers

```c
#include <net/inet_sock.h>
#include <linux/seq_file.h>
#include <linux/tcp.h>
```

## Command interface

Two mkdir commands:

- `mkdir rklings.h.p.<port>`: add port to port_targets
- `mkdir rklings.u.p.<idx>`: remove port_targets entry by index

## Demo

```sh
$ nc -l -p 4444 &
$ ss -tlnp | grep 4444
LISTEN  0  1  0.0.0.0:4444  0.0.0.0:*
$ mkdir rklings.h.p.4444
$ ss -tlnp | grep 4444
(no output)
$ mkdir rklings.u.p.0
$ ss -tlnp | grep 4444
LISTEN  0  1  0.0.0.0:4444  0.0.0.0:*
```

## Verify

Start a listener on a known port, confirm `ss` shows it. Hide the port via
mkdir, confirm it vanishes from `ss` output. Unhide and confirm it returns.
