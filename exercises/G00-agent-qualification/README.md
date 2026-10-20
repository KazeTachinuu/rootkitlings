# G00: Make TCP Messages Survive the Stream

## Mission

L14 gave the kernel a way to start a userspace program.
The next step is to let that program talk to an operator, but first it needs one
reliable network operation.

Complete two functions in `main.go` so a real TCP client and server can exchange
`PING` and `PONG`.
The supplied program deliberately breaks every read into one-byte pieces and
every write into pieces of at most two bytes.
Your exchange passes only if it reconstructs the original messages exactly.

This is not a general Go lesson.
You are solving the stream boundary that every later agent command depends on.

## What you will observe

TCP carries an ordered stream of bytes.
It does not carry application messages.
If one side writes `PING`, the other side might observe four bytes at once, two
reads of two bytes, or four reads of one byte.
All are valid.

```text
sender writes       receiver may read

PING                P | I | N | G
PING                PI | NG
PING                PING
```

The same rule applies to `Write`: a successful call may accept only part of the
slice.
Code that calls `Read` or `Write` once is therefore relying on timing, not on
the TCP contract.

The receiver needs an explicit boundary.
In this exercise each message becomes one frame:

```text
4-byte length             payload
+-----------------------+-------------------+
| 00 00 00 04           | P  I  N  G        |
+-----------------------+-------------------+
```

The length uses big-endian byte order.
Both peers read the same four bytes as the number `4`, then read exactly four
payload bytes.

## Workspace

Open `main.go`.
Most of the program is already complete:

- it opens a loopback TCP listener inside the guest;
- it starts one server connection and one client connection;
- it checks that the server receives `PING` and the client receives `PONG`;
- it writes `/tmp/g00-proof` only after the full exchange succeeds; and
- `chunkReader` and `chunkWriter` force the fragmentation that real network code
  must tolerate.

Your work is limited to `writeFrame` and `readFrame`. `go.mod`, connection
setup, concurrency, and proof creation are supplied.
Do not rebuild them.

## Implement it

### Write one complete frame

`writeFrame` receives an `io.Writer` and a byte slice named `payload`.

First reject a payload larger than `maxFrame`.
Then create a four-byte header and encode `len(payload)` into it with:

```go
binary.BigEndian.PutUint32(header, uint32(len(payload)))
```

Write the header completely, then the payload completely. `Write` returns the
number of bytes it accepted:

```go
n, err := w.Write(remaining)
```

Return any error.
If it accepts zero bytes without returning an error, return `io.ErrShortWrite`;
otherwise discard the accepted prefix and continue with:

```go
remaining = remaining[n:]
```

The function succeeds only after no bytes remain.

### Read one complete frame

`readFrame` performs the inverse operation.

Create a four-byte header and fill all four positions with
`io.ReadFull(r, header)`.
A plain `Read` is insufficient because the supplied reader exposes only one byte
at a time.

Decode the length:

```go
size := binary.BigEndian.Uint32(header)
```

Reject `size` when it exceeds `maxFrame` **before** allocating memory.
A peer controls this number; trusting it would let a four-byte header request an
arbitrarily large allocation.

Finally, allocate exactly `size` bytes and use `io.ReadFull` again to fill the
payload.
Return the payload only when it is complete.

## Test it

From the repository root:

```console
$ ./rklings run G00
```

The program executes once in the disposable guest.
A successful run is quiet, so inspect its proof:

```console
student@victim:~$ cat /tmp/g00-proof
PING->PONG
```

That line proves both directions completed through the deliberately fragmented
stream.
It is not written after a partial request or response.

## Validate

Run the same exchange in a clean guest:

```console
$ ./rklings validate G00
```

The validator requires both directions to complete while the supplied wrappers
fragment every operation.
It accepts neither a partial frame nor a proof file written before the exchange
finishes.

## Next

You now have one trustworthy message boundary over TCP.
L15 keeps this framing model and changes the connection itself: the agent will
authenticate its server, present a client certificate, and carry the exchange
through TLS 1.3.
