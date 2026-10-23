# L20: Fileless Module Loading

## Mission

L19 transferred files over TLS, but the `.ko` landed on disk. This exercise
embeds the module in the agent, places its bytes in a memory-backed file, and
asks the kernel to load it from that descriptor.

## What you will observe

The module becomes active without a `rklings.ko` directory entry:

```console
root@victim:~# grep '^rklings ' /proc/modules
rklings 16384 0 - Live 0x0000000000000000
root@victim:~# find / -name rklings.ko 2>/dev/null
```

The empty `find` result is the point. The kernel received the same module bytes
as before, but those bytes never occupied a named file in the guest.

## Workspace

```text
exercises/L20-fileless-module
├── go.mod
├── go.sum
└── main.go
```

Complete the TODOs in `main.go`. The build embeds the reference module in the
loader as `rklings.ko`; you implement the path from those embedded bytes to the
kernel.

## Follow the bytes into the kernel

Linux provides two primitives that, combined, let you load a kernel module
without any file on disk:

```text
+-----------------------------+
| loader executable           |
│   embedded rklings.ko bytes |
+--------------+--------------+
               | memfd_create
               v
+-----------------------------+
| anonymous RAM-backed file   |
│   write all module bytes    |
+--------------+--------------+
               | finit_module(fd, "", 0)
               v
+-----------------------------+
| running kernel              |
│   rklings module loaded     |
+-----------------------------+
```

`memfd_create` creates an anonymous file backed by RAM:

- Appears under `/proc/self/fd/` while open but has no directory entry anywhere
- The `MFD_CLOEXEC` flag ensures the fd is closed automatically if the process
  execs another binary

`finit_module` is the fd-based variant of `init_module`:

- `init_module` takes a pointer to the module bytes in userspace memory
- `finit_module` takes a file descriptor
- The kernel reads the ELF from the fd, verifies it, and loads it
- Same result as `insmod`, but the source can be a memfd with no filesystem path

## TODOs

```text
TODO 1  main.go:28:2  create a memfd with MFD_CLOEXEC and defer its close
TODO 2  main.go:30:2  write .ko bytes to the memfd
TODO 3  main.go:32:2  load the module with finit_module
```

### TODO 1 -- `main.go:28:2` -- Create a memfd with MFD_CLOEXEC and defer its close

1. Call `unix.MemfdCreate` with an empty name and `unix.MFD_CLOEXEC` (the name
   is cosmetic, visible in `/proc/pid/fd/`)
2. Immediately defer `unix.Close(fd)` so the descriptor is released when the
   function returns
3. Return the error wrapped with context if creation fails

### TODO 2 -- `main.go:30:2` -- Write .ko bytes to the memfd

Write the full `ko` slice to the fd with `unix.Write`. Return on error.

### TODO 3 -- `main.go:32:2` -- Load the module with finit_module

Call `unix.FinitModule(fd, "", 0)` to ask the kernel to load the module from the
open descriptor. The empty string means no module parameters, and zero flags
request the default load behavior.

## Test it

```console
$ ./rklings run L20
```

`run` stages the loader, executes it once, and leaves the guest open for
inspection:

```console
root@victim:~# lsmod | grep rklings
rklings                16384  0
root@victim:~# find / -name 'rklings.ko' 2>/dev/null
root@victim:~#
```

The module shows in `lsmod`, but no `.ko` file exists in a filesystem directory.
The loader closes the memfd before it exits, so no open descriptor remains.

## Validate

```console
$ ./rklings validate L20
  [+] rklings module is loaded in the kernel
  [+] no .ko file exists anywhere on disk
  [+] module was loaded via memfd (no file path in /proc)
  PASS
```

## Next

The module loads without leaving a `.ko` in a directory, but the launcher still
remains on disk. L21 removes that launcher after startup while keeping the agent
running.
