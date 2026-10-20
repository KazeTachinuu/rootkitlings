# L18: Remote Shell

## Mission

Add a `SHELL` request that runs one command and returns its combined output and
exit failure over the authenticated connection.

L17 dispatches commands that map to mkdir calls.
But an operator needs to run arbitrary shell commands on the target.
One new handler adds remote execution.

## What you will observe

`SHELL id` returns root identity, shell pipelines and redirections retain their
normal meaning, and a nonexistent command returns its diagnostic without
breaking the connection.
A line containing only `.` terminates every response so the next request starts
at a known boundary.

## Workspace

```text
exercises/L18-remote-shell
├── go.mod
└── main.go
```

The existing handlers and dispatch loop are provided.
Only add `handleShell` and register it in the handler table.

## exec.Command

Go's `os/exec` package runs external programs. `exec.Command("sh", "-c", args)`
passes the entire argument string to `sh`, which handles pipes, redirects,
globs, and everything else the shell provides.

`CombinedOutput()` runs the command, waits for it to finish, and returns stdout
and stderr together as a byte slice.

```text
  Server                         Agent
    |                              |
    |--- "SHELL id\n" ------------>|
    |                              |  sh -c "id"
    |                              |  output: "uid=0(root)...\n"
    |<-- "uid=0(root)...\n\n" ----|
    |                              |
    |--- "SHELL cat /etc/host*" -->|
    |                              |  sh -c "cat /etc/host*"
    |<-- "hostname\n127.0...\n" --|
```

The response is the raw command output followed by a line containing only `.`.
That terminator lets the server read multiline output without guessing where it
ends.

## Response algorithm

Implement the response in this order without copying a completed handler:

1. execute `/bin/sh -c <argument>` and retain combined stdout and stderr;
2. report a nonzero exit without discarding any captured output;
3. write every output byte, adding a newline only when the output lacks one;
4. write a line containing only `.` even for empty output or failure; and
5. return transport errors separately from the command's exit status.

The terminator is mandatory: omitting it desynchronizes the next command on the
same connection.

## Implement it

Complete `main.go`:

- Everything from L17 (mTLS, dispatch loop, handler map)
- One new entry in the handler map: SHELL
- `handleShell`: exec.Command, CombinedOutput, write result to conn

### Interfaces used here

```go
import "os/exec"

out, err := exec.Command("sh", "-c", arg).CombinedOutput()
// out is []byte containing stdout+stderr
// err is non-nil if the command exits non-zero
```

## Test it

`rklings serve L18` starts this exercise's live one-shot execution panel at
`http://127.0.0.1:8080`.
Output shown there is returned by the guest agent.

```sh
./rklings serve L18
```

In another host terminal:

```sh
./rklings run L18
```

Open `http://127.0.0.1:8080`.
Enter a command in the shell field.
The output and failure status shown by the panel come from the guest process.

```
server> SHELL id
agent:  uid=0(root) gid=0(root) groups=0(root)
server> SHELL cat /etc/hostname
agent:  victim
server> SHELL ls /nonexistent
agent:  ERR exit status 2
server> PING
agent:  PONG
```

Stop `run` and `serve` with `Ctrl+C` when finished.

## Validate

```sh
./rklings validate L18
  [+] agent connects over mTLS
  [+] SHELL id returns uid=0
  [+] SHELL handles pipes and redirects
  [+] SHELL reports error for bad command
  [+] PING still works alongside SHELL
  PASS
```

## Next

You can execute commands remotely, but moving a binary or retrieving a file is
still awkward.
L19 adds upload and download over the same authenticated connection.
