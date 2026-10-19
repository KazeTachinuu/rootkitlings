# L17: Command Dispatch

Turn the one-purpose bridge into a request/response agent.
Each received line selects one handler, and every handler returns an explicit
protocol response.

L16 proved the agent can drive the kernel module through `mkdir`, but it only
ran a fixed sequence.
This exercise reads commands from the operator and routes each command to a
handler.

## Workspace

```text
exercises/L17-command-dispatch
├── go.mod
└── main.go
```

Only `main.go` is editable.
The TLS setup and mkdir helper are provided; implement the handler type, table,
handlers, and loop in that order.

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
Each handler receives the argument string (everything after the first space) and
the connection, and returns an error.

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

The dispatch loop reads one line, splits on the first space to get the command
and argument, looks up the handler, and calls it.
Unknown commands get an "ERR unknown command" response.

## Commands

| Command        | Argument | mkdir            | Response |
| -------------- | -------- | ---------------- | -------- |
| PING           | (none)   | (none)           | PONG     |
| STATUS         | (none)   | rklings.status   | OK       |
| HIDE FILE name | name     | rklings.h.f.name | OK       |
| HIDE PID pid   | pid      | rklings.h.i.pid  | OK       |
| HIDE PORT port | port     | rklings.h.p.port | OK       |
| PERSIST        | (none)   | rklings.persist  | OK       |
| ESCALATE       | (none)   | rklings.e        | OK       |
| QUIT           | (none)   | (none)           | (close)  |

HIDE takes a subcommand (FILE, PID, PORT) as its first argument word.

## Implement it

Complete `main.go`:

- mTLS dial and connection setup (from L15)
- `bufio.Scanner` read loop over the TLS connection
- Handler map with dispatch logic
- Handlers for PING, STATUS, HIDE, PERSIST, ESCALATE, QUIT

## Useful interfaces

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

`rklings serve L17` starts this exercise's live server and scoped command panel
at `http://127.0.0.1:8080`.
Every log entry is traffic exchanged with the agent running in the disposable
guest.

```sh
./rklings serve L17
```

In another host terminal:

```sh
./rklings run L17
```

Open `http://127.0.0.1:8080`.
The panel sends only the commands introduced by this exercise, and its log shows
the real replies from the guest agent.
Create `secret.txt` in the guest before sending the hide command.

```
server> PING
agent:  PONG
server> STATUS
agent:  OK
server> HIDE FILE secret.txt
agent:  OK
root@victim:~# ls    (secret.txt hidden)
```

Stop `run` and `serve` with `Ctrl+C` when finished.

## Validate

```sh
./rklings validate L17
  [+] agent connects over mTLS
  [+] PING returns PONG
  [+] STATUS triggers mkdir rklings.status
  [+] HIDE FILE x hides file from ls
  [+] ESCALATE triggers privilege escalation
  PASS
```

## Next

The operator can invoke the built-in controls, but cannot run an arbitrary
command on the target.
L18 adds remote command execution and returns its output.
