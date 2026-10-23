# G00: Make TCP Messages Survive the Stream

## Mission

Up to now every exercise was kernel C. The kernel module:

- Intercepts syscalls
- Hides files
- Escalates privileges

But it cannot open a network socket or speak TLS. Those are userspace jobs, so
the rest of the course builds a companion agent that runs alongside the module
and connects back to the operator.

The agent is written in Go. Go compiles to a single static binary with no
runtime dependencies, which matters when the binary must be embedded,
transferred, or executed from memory on a target that has no toolchain. Its
standard library includes TLS, HTTP, and cross-platform syscall wrappers, so the
agent needs no external C libraries.

L14 gave the kernel a way to start that agent. Before the agent can talk to an
operator, it needs one reliable network primitive: reading and writing complete
messages over a TCP byte stream.

Complete two functions in `main.go` so a client and server exchange `PING` and
`PONG`.

The supplied program deliberately fragments every read into single bytes and
every write into pieces of at most two bytes. Your exchange passes only if it
reconstructs the original messages exactly.

## What you will observe

TCP carries an ordered stream of bytes. It does not carry application messages.
If one side writes `PING`, the other side might observe four bytes at once, two
reads of two bytes, or four reads of one byte. All are valid.

```text
sender writes       receiver may read

PING                P | I | N | G
PING                PI | NG
PING                PING
```

The same rule applies to `Write`: a successful call may accept only part of the
slice. Code that calls `Read` or `Write` once is therefore relying on timing,
not on the TCP contract.

The receiver needs an explicit boundary. In this exercise each message becomes
one frame:

```text
4-byte length             payload
+-----------------------+-------------------+
| 00 00 00 04           | P  I  N  G        |
+-----------------------+-------------------+
```

The length uses big-endian byte order. Both peers read the same four bytes as
the number `4`, then read exactly four payload bytes.

## Workspace

```text
exercises/G00-agent-qualification
├── go.mod
└── main.go
```

Open `main.go`. Most of the program is already complete:

- it opens a loopback TCP listener inside the guest;
- it starts one server connection and one client connection;
- it checks that the server receives `PING` and the client receives `PONG`;
- it writes `/tmp/g00-proof` only after the full exchange succeeds; and
- `chunkReader` and `chunkWriter` force the fragmentation that real network code
  must tolerate.

Your work is limited to `writeFrame` and `readFrame`. `go.mod`, connection
setup, concurrency, and proof creation are supplied. Do not rebuild them.

## Go in 60 seconds

If you have not written Go before, here is enough to read the starter file.

Go functions return errors explicitly. There are no exceptions.

```go
n, err := w.Write(data)
if err != nil {
    return err
}
```

A slice (`[]byte`) is a view over an array, like a pointer and length pair in C.
`len(s)` returns its length. `s[n:]` creates a new slice starting at offset `n`.

```go
remaining := payload
for len(remaining) > 0 {
    n, err := w.Write(remaining)
    remaining = remaining[n:]
}
```

`make([]byte, size)` allocates a new slice of the given length, like `malloc`.
The garbage collector frees it when nothing references it.

Imports at the top of the file name standard library packages.
`binary.BigEndian.PutUint32` and `binary.BigEndian.Uint32` encode and decode a
32-bit integer in network byte order. `io.ReadFull` repeats reads until the
destination slice is completely filled.

The rest of the supplied code uses structs and methods. `func (w chunkWriter)
Write(p []byte)` is a method on the `chunkWriter` type, equivalent to passing a
struct pointer as the first argument in C. You do not need to write any methods
in this exercise.

## TODOs

```text
TODO 1  main.go:39:2  reject payload larger than maxFrame
TODO 2  main.go:40:2  encode payload length as big-endian header, write it
TODO 3  main.go:41:2  write every payload byte, retrying short writes
TODO 4  main.go:46:2  read the four-byte header with io.ReadFull
TODO 5  main.go:47:2  decode the frame size and reject values above maxFrame
TODO 6  main.go:48:2  allocate the payload and read it completely with io.ReadFull
```

### TODO 1 -- `main.go:39:2` -- Reject oversized payloads

Return an error if `len(payload)` exceeds `maxFrame`.

### TODO 2 -- `main.go:40:2` -- Encode and write the header

Create a four-byte slice and encode the payload length into it:

```go
binary.BigEndian.PutUint32(header, uint32(len(payload)))
```

Write all four bytes. `Write` may accept fewer bytes than requested, so loop
until none remain.

### TODO 3 -- `main.go:41:2` -- Write every payload byte

Loop over the payload the same way. Each `Write` returns the number of bytes it
accepted:

```go
n, err := w.Write(remaining)
```

Return any error. If it accepts zero bytes without returning an error, return
`io.ErrShortWrite`, the standard sentinel for a write that made no progress.
Otherwise advance past the accepted prefix with `remaining = remaining[n:]`.

### TODO 4 -- `main.go:46:2` -- Read the header

Create a four-byte slice and fill it with `io.ReadFull(r, header)`.
`io.ReadFull` repeats reads until the slice is completely filled or an error
occurs. A plain `Read` is insufficient because the supplied reader exposes only
one byte at a time.

### TODO 5 -- `main.go:47:2` -- Decode and validate the size

```go
size := binary.BigEndian.Uint32(header)
```

Reject `size` above `maxFrame` **before** allocating memory. A peer controls
this number; trusting it would let a four-byte header request an arbitrarily
large allocation.

### TODO 6 -- `main.go:48:2` -- Read the payload

Allocate exactly `size` bytes with `make([]byte, size)` and fill them with
`io.ReadFull`. Return the payload only when it is complete.

## Test it

From the repository root:

```console
$ ./rklings run G00
```

The program executes once in the disposable guest. A successful run is quiet, so
inspect its proof:

```console
student@victim:~$ cat /tmp/g00-proof
PING->PONG
```

## Validate


```console
$ ./rklings validate G00
  [+] Length-framed exchange survives stream boundaries
  PASS
```


## Next

You now have one trustworthy message boundary over TCP. L15 keeps this framing
model and changes the transport: the agent will derive a shared key via X25519,
encrypt every message with AES-256-GCM, and connect to the operator over
WebSocket.
