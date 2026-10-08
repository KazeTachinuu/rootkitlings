# 39 - Remove one socket row from `/proc/net/tcp`

`/proc/net/tcp` is rendered through a `seq_file` callback for each TCP socket.
This module suppresses the row whose local port is 8080 without removing or closing the socket.

## Observe it

```sh
nc -l -p 8080 &
cat /proc/net/tcp | grep ':1F90'
insmod hello.ko
cat /proc/net/tcp | grep ':1F90'
cat /proc/rk_portcloak
```

```text
port=8080 hidden=<positive count>
```

The first TCP-table search finds the listener and the second does not.
An external connection or inet-diag query remains an independent way to find it.

## Rewind one seq-file row

`tcp4_seq_show(seq, socket)` appends one formatted row by advancing `seq->count`.
The entry handler saves that count and decides whether the current socket matches.
The return handler restores the saved count for a matching socket, effectively discarding only the new row.

```text
saved count ---> tcp4_seq_show appends row ---> matching port?
                                                   | yes
                                                   v
                                      restore saved count
```

`tcp4_seq_show` is a normal kernel function, not a syscall wrapper.
Its first two arguments are directly in `regs->di` and `regs->si`; there is no nested `pt_regs` lookup.
`SEQ_START_TOKEN` represents the table header and must always pass through.
The local port is stored in network byte order and is compared with `htons(HIDDEN_PORT)`.

## Implement it

```sh
./rootkitlings start L39
$EDITOR .rootkitlings/work/socket-row-filtering/hello.c
```

In the marked matching branch, set `ctx->hide = true`.

The argument capture, saved count, header handling, return-time rewind, counter, proc status, and lifecycle are provided.

## Verify

```sh
./rootkitlings check L39
```

The check starts a real listener, observes it before loading, requires it to disappear from `/proc/net/tcp`, confirms the counter, and verifies that the row returns after unload.

Modern `ss` commonly uses inet-diag netlink rather than this proc renderer, so disagreement between those views exposes the filter.
