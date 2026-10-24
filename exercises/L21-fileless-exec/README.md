# L21: Fileless Agent Execution

## Mission

L20 loaded a kernel module without writing a .ko to disk, but:

- The loader binary itself was a file
- Any agent binary you deploy is a file too
- Forensics finds both with a simple `find`

This exercise eliminates the binary from disk entirely by writing it to
anonymous memory and executing from there.
The process runs, but there is no file backing it.

## What you will observe

The agent keeps running after its launcher disappears:

```console
student@victim:~$ readlink /proc/482/exe
/memfd: (deleted)
student@victim:~$ find / -type f -name agent 2>/dev/null
```

`/proc/482/exe` proves that PID 482 still has executable code.
The empty `find` result proves there is no corresponding directory entry.

## Workspace

```text
exercises/L21-fileless-exec
├── go.mod
├── go.sum
└── main.go
```

The build embeds the supplied agent in the dropper.
Complete `main.go` so the dropper replaces itself with that embedded program.

## Replace the launcher from memory

The trick is the same building block (memfd_create) with one critical
difference: no `MFD_CLOEXEC`.

```text
+----------------------------+
| dropper process            |
|   embedded agent bytes     |
+-------------+--------------+
              | write to memfd
              v
+----------------------------+
| /proc/self/fd/N            |
|   anonymous executable     |
+-------------+--------------+
              | exec
              v
+----------------------------+
| same PID, new program      |
|   /proc/PID/exe -> memfd   |
+----------------------------+
```

Why this exercise omits `MFD_CLOEXEC`: execution is requested through the
`/proc/self/fd/N` pathname.
Keeping the descriptor open avoids depending on the exact point at which that
pathname is resolved relative to close-on-exec processing and leaves the new
image with the backing descriptor.
This is a deliberate lifetime choice, not a general rule that every executable
memfd must survive `exec`.

Why `/proc/self/fd/N`: exec needs a path.
A memfd has no directory entry, but every open fd has a symlink at
`/proc/self/fd/<number>`.
The kernel follows this symlink and reads the executable from the memfd.

## TODOs

```text
TODO 1  main.go:25:2  create the memfd
TODO 2  main.go:27:2  write the agent bytes
TODO 3  main.go:29:2  build the fd path
TODO 4  main.go:31:2  exec into the agent
```

### TODO 1 -- `main.go:25:2` -- Create the memfd

Call `unix.MemfdCreate("", 0)` with no flags.
Passing 0 instead of `unix.MFD_CLOEXEC` keeps the descriptor open across exec.

### TODO 2 -- `main.go:27:2` -- Write the agent bytes

Write the embedded binary to the memfd with `unix.Write`.
On failure, close the fd and return the error.

### TODO 3 -- `main.go:29:2` -- Build the fd path

Format `/proc/self/fd/<N>` where N is the memfd descriptor number.
This is the only pathname the kernel can use to reach the anonymous file.

### TODO 4 -- `main.go:31:2` -- Exec into the agent

`unix.Exec` replaces the current process image with a new program, like the C
`execve` syscall:

- It takes a path, an argv slice, and an environment slice
- `argv[0]` becomes the process name shown by `ps`, so `[]string{"agent"}` makes
  the process appear as "agent"
- Pass `nil` for the environment

## Test it

```console
$ ./rklings run L21
```

`run` starts the staged dropper in the background and removes the staged file.
Inspect the resulting process inside the guest:

```console
student@victim:~$ pgrep -a agent
1587 agent
student@victim:~$ ls -la /proc/1587/exe
lrwxrwxrwx 1 student student 0 ... /proc/1587/exe -> /memfd: (deleted)
student@victim:~$ find / -name 'agent' -type f 2>/dev/null
student@victim:~$
```

The agent is running. `/proc/pid/exe` points to a memfd, not a file on disk.
`find` returns nothing because no binary was ever written to the filesystem.

## Validate

```console
$ ./rklings validate L21
  [+] agent process is running
  [+] no agent binary exists on disk
  [+] /proc/pid/exe shows (deleted)
  PASS
```

## Next

The target no longer needs a module or agent file after startup, but anyone who
connects to the server still reaches the operator endpoint.
L22 shows ordinary visitors a decoy page while authenticated agents reach the
command channel on the same port.
