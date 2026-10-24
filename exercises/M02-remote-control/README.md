# M02: Remote Control

## Mission

L15 through L19 built the pieces:

- AEAD transport
- mkdir bridge
- Command dispatch
- Shell execution
- File transfer

M02 assembles them into one remote-control system:

- The operator provides a terminal and browser dashboard on the host
- The agent connects over a persistent WebSocket with AES-GCM encryption
- The agent executes commands directly or translates them into kernel commands

## What you will observe

Start the target and operator independently.
The agent connects via WebSocket, and commands sent from the operator cross the
encrypted channel and return the target's real output immediately:

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

The starter and reference use the same protocol, dependencies, PTY package, and
browser dashboard. `tui.go` and `web.go` are complete course-supplied operator
surfaces: read them to understand their calls, but do not recreate them.
The editable integration seams are `cmd/agent/main.go` and `cmd/server/main.go`.

Complete both ends of the protocol.
The agent owns target-side execution; the server owns crypto routing, session
management, and the two operator views.

## Architecture

```text
+--------------------+
| OPERATOR HOST      |
| terminal prompt    |
| browser UI :8080   |
| WebSocket sessions |
+----------+---------+
           | WebSocket + AES-GCM
           v
+--------------------+
| TARGET VM          |
| persistent WS conn |
| AEAD decrypt       |
| dispatch + execute |
+----------+---------+
           | mkdir control
           v
+--------------------+
| rklings.ko         |
| local controls     |
+--------------------+
```

## Transport Protocol

### Key Material

The agent embeds at compile time two 32-byte X25519 keys: `keys/agent.key`
(private) and `keys/operator.pub` (public).

The server loads the operator private key and the agent public key from files
given as command-line arguments.

Both sides derive the same shared key:

```text
secret     = ECDH_X25519(my_private, their_public)
shared_key = HKDF(SHA-256, secret, info="rklings-aead")
agent_id   = hex(HKDF(SHA-256, secret, info="rklings-id")[:8])
```

### Wire Format

The agent opens a WebSocket connection to the operator.
The first binary frame is the hello, which authenticates the agent:

```text
agent_public_key (32 bytes) || AES_GCM_Seal({"t":"hello"})
```

The server compares the first 32 bytes against the known agent public key, then
decrypts the remainder with the shared key.
On success the connection is authenticated.

All subsequent frames in both directions are standalone AEAD ciphertext:

```text
nonce (12 bytes) || ciphertext+tag
```

Commands from operator to agent:

```json
{ "op": "SHELL", "arg": "id" }
```

Results from agent to operator:

```json
{ "data": { "stdout": "uid=0(root)...\n", "exit_code": 0 } }
```

PTY output streams as `{"pty":"<base64>"}` frames pushed by the agent.

If the hello fails (wrong key, unknown agent ID), the server closes the
connection silently.
Non-WebSocket requests receive a decoy HTML page.

### Session lifecycle

- WebSocket connect = agent registers
- WebSocket close = agent disconnects
- No polling interval, no lastSeen heuristic
- The connection is persistent and bidirectional

### Crypto (Go stdlib + coder/websocket)

- `crypto/ecdh` for X25519
- `crypto/aes` + `crypto/cipher` for AES-256-GCM
- `crypto/sha256` for key derivation
- `crypto/rand` for nonces
- `coder/websocket` for the WebSocket transport

## Command set

| Command          | Agent action                       |
| ---------------- | ---------------------------------- |
| PING             | Return "PONG"                      |
| STATUS           | Return pid and uid as JSON         |
| INFO             | Return JSON state object           |
| SHELL cmd        | exec sh -c cmd, return JSON result |
| PTY START        | Allocate PTY, return "OK"          |
| PTY INPUT b64    | Write to PTY, return "OK"          |
| PTY RESIZE c r   | Set PTY window size, return "OK"   |
| PTY CLOSE        | Close PTY, return "OK"             |
| DOWNLOAD path    | Stream chunked binary (0x01/0x02)  |
| UPLOAD           | Receive {path,data} in cmd.Data    |
| HIDE FILE name   | mkdir rklings.h.f.name             |
| HIDE PID pid     | mkdir rklings.h.i.pid              |
| HIDE PORT port   | mkdir rklings.h.p.port             |
| HIDE CONTENT str | mkdir rklings.h.c.str              |
| UNHIDE type val  | mkdir rklings.u.type.val           |
| PERSIST          | mkdir rklings.persist              |
| ESCALATE         | mkdir rklings.e                    |
| ESCALATE pid     | mkdir rklings.e.pid                |

PTY output is streamed: the agent pushes `{"pty":"<base64>"}` frames over the
WebSocket as output arrives.
No polling needed.

## Operator interfaces

The same process provides two surfaces:

- A line-oriented terminal prompt
- A browser dashboard at `127.0.0.1:8080`

Both send commands over the agent's WebSocket session and receive results
immediately.
The terminal writes downloaded files to disk; the browser provides a preview and
save action.

## TODOs

```text
TODO 1  cmd/server/main.go   implement WebSocket agent authentication
TODO 2  cmd/server/main.go   implement command send and response wait
TODO 3  cmd/agent/main.go    implement the PTY lifecycle
```

M02 is an integration milestone, not a rewrite of L15-L19.
Complete exactly three seams.

Already supplied and declared as black boxes:

- crypto functions (deriveKeys, sealAEAD, openAEAD, newGCM);
- WebSocket transport and session management types;
- L17-L19 command handlers adapted for return values;
- terminal command parser;
- built browser dashboard and WebSocket relay;
- upload relay, snapshots, and event history.

The milestone asks you to connect these components without corrupting their
shared protocol.
It does not ask you to reproduce the product operator.

### TODO 1: Implement WebSocket agent authentication

1. Check if the request is a WebSocket upgrade; if not, serve the decoy page
2. Upgrade the connection
3. Read the first binary frame
4. Extract the 32-byte agent public key prefix and verify it matches the known
   agent public key
5. Decrypt the remaining ciphertext with `openAEAD`
6. If any step fails, close the connection

On success:

1. Create an `agentSession` with the connection, shared key, a buffered response
   channel, and a done channel
2. Register the session
3. Enter the read pump: decrypt each frame and route PTY output to subscribers
   or command responses to the response channel

### TODO 2: Implement command send and response wait

Encrypt a command as JSON with `sealAEAD`, write it to the agent's WebSocket
connection (holding the write mutex), and wait on the response channel with a
30-second timeout.
Return the result or a timeout error.

### TODO 3: Implement the PTY lifecycle

Handle START, INPUT, RESIZE, and CLOSE against `shellPTY`.
Encode all binary I/O as base64.
Protect shared PTY state with its mutex.

- **START**: allocate a PTY shell with `pty.StartWithSize`, store the fd, start
  a goroutine that reads PTY output and sends `{"pty":"<base64>"}` frames via
  `wsSend`.
  Return "OK".
- **INPUT**: decode the base64 payload and write it to the PTY.
  Return "OK".
- **RESIZE**: parse columns and rows, call `pty.Setsize`.
  Return "OK".
- **CLOSE**: close the PTY file and clear the state.
  Return "OK".

## Test it

Two terminals.

### 1. Boot the target

```console
$ ./rklings run M02
```

The VM loads the lesson module and starts the agent, which connects to the
operator via WebSocket as soon as one is available.

### 2. Start the operator

In a second host terminal:

```console
$ ./rklings serve M02
```

The `rklings>` prompt appears.
The agent's WebSocket connection shows it connected.

### Terminal commands

```text
rklings> ping
PONG
rklings> shell uname -r
7.0.0-38-generic
rklings> hide file secret.txt
OK
rklings> download /etc/hostname
[+] saved 7 bytes -> .rklings/downloads/etc/hostname
```

Commands execute and return results immediately over the persistent connection.

### Browser dashboard

Open `http://127.0.0.1:8080` in a browser.
The connected agent appears in the target list.

The dashboard provides:

- Browser-embedded PTY shell
- File browser with upload and download
- Live process and port snapshots
- Connection events

Run a command from the browser shell panel to confirm both surfaces share the
same agent session.

## Validate

```console
$ ./rklings validate M02
```

```console
  [+] operator starts on isolated dynamic ports
  [+] agent connects and responds to commands
  [+] PING/PONG round-trip
  [+] SHELL executes and returns output
  [+] DOWNLOAD transfers file correctly
  [+] HIDE FILE triggers mkdir in VM
  [+] no plaintext commands in captured traffic
  [+] operator relay and web listener respond
  PASS
```

## Next

The operator can control the target remotely, but the deployed files, process,
and local activity remain easy to inspect.
L20 through L24 reduce each of those visible deployment traces in turn.
