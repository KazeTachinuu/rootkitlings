# Hide a listening port from /proc/net/tcp

When you bind a TCP listener, the kernel adds its socket to an internal table.
Tools like `ss` and `netstat` read `/proc/net/tcp`, which iterates that table
using the seq_file interface: for each socket, the kernel calls `tcp4_seq_show`
to format one line of output.

## The hook point

A naive approach would hook `vfs_read` on `/proc/net/tcp` and edit the buffer.
That works but is fragile: you parse text the kernel just produced, matching
hex-encoded port numbers in a fixed-width format.

The idiomatic technique hooks `tcp4_seq_show` directly.
Each call receives a `struct seq_file *` and a `void *v` pointing to the current
`struct sock`.
A kprobe pre-handler that returns 1 causes the probed function to be skipped
entirely: the socket's line never reaches the output buffer.

```text
                  seq_file iteration
                        |
    tcp4_seq_show(seq, v)     <-- kprobe fires here
         |                          |
    v == SEQ_START_TOKEN?      return 0: let it print header
         |
    inet_sk(v)->inet_sport     compare to hidden_port
         |                          |
    match?                     return 1: skip (hide)
         |                          |
    no match                   return 0: let it print
```

## Register layout (x86_64 kernel 7.2)

On x86_64, `tcp4_seq_show(struct seq_file *seq, void *v)` receives:

- `regs->di` = first argument (`seq`)
- `regs->si` = second argument (`v`)

The `v` pointer is `SEQ_START_TOKEN` for the header row.
For data rows, cast it to `struct sock *` and use `inet_sk()` to access the
`inet_sock` fields.
The source port is `inet->inet_sport` in network byte order; use `ntohs()` to
compare with a host-order port number.

## ABI dependency

This lesson depends on kernel 7.2's `tcp4_seq_show` symbol and calling
convention.
The function exists in mainline Linux but its signature and internal behavior
can change between kernel versions.

## Your task

Edit `main.c`.

Implement `rk_tcp_show_pre`: check the port of each socket and return 1 to hide
it, 0 to let it through.
The `hidden_port` module parameter is already wired up.

Run:

```sh
./rklings check
```

## References

- [seq_file interface][1]
- [Linux v7.2 `tcp4_seq_show` and TCP seq_file iteration][2]
- [Linux v7.2 `inet_sock` and port fields][3]

[1]: https://docs.kernel.org/7.2/filesystems/seq_file.html
[2]: https://github.com/torvalds/linux/blob/v7.2/net/ipv4/tcp_ipv4.c
[3]: https://github.com/torvalds/linux/blob/v7.2/include/net/inet_sock.h
