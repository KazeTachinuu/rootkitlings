# L22: Decoy Routing

## Mission

L21 made the agent binary fileless, but the server still has a visible command
channel. An administrator or scanner who connects may notice unusual behavior.

This exercise hides the C2 channel inside ordinary HTTP traffic using
symmetric cryptography as the routing layer.

## What you will observe

Every HTTP request to the server returns the same innocent HTML page.
The agent's WebSocket connection is the only path that reaches the C2 handler:

```console
$ curl -s http://127.0.0.1:8080/ | head -1
<!DOCTYPE html>
$ curl -s http://127.0.0.1:8080/secret | head -1
<!DOCTYPE html>
```

No 404, no 403, no path-based routing. An observer sees a normal website.

## Concept

Every request to the server, regardless of method, path, or headers,
receives a 200 OK with a plausible HTML page. There is no special
endpoint, no unusual response code, and no header that distinguishes
an agent connection from a browser visit.

The routing decision happens entirely in the crypto layer:

- Only a valid WebSocket upgrade whose first binary frame passes AEAD
  authentication reaches the C2 handler
- Everything else gets the decoy page

```text
Request arrives
      |
      v
Is it a WebSocket upgrade?
      |
      └── no ---------------------------------> decoy page (200 OK)
      |
      v
Upgrade, read first binary frame
      |
      └── too short (< 32+12+16 bytes) ------> close
      |
      v
Extract agent public key (first 32 bytes), derive shared key
      |
      └── key derivation fails --------------> close
      |
      v
AES-GCM decrypt hello with the derived shared key
      |
      ├── decryption fails ------------------> close
      └── decryption succeeds ---------------> authenticated session
```

A network observer sees only HTTP requests and 200 OK responses for
regular visitors. Agent connections upgrade to WebSocket, which looks
like any other WebSocket application (chat, notifications, dashboards).

## Wire format

The agent opens a WebSocket connection. The first binary frame is:

```text
agent_pub (32 bytes) || AES_GCM_Seal({"t":"hello"})
```

The server extracts the 32-byte X25519 public key, derives the shared
key via ECDH, and tries decryption. On success, the connection is
authenticated and stays open for encrypted command frames. On any
failure, the connection is closed.

Non-WebSocket requests always receive the decoy page.

## Key material

In the real operator, keys come from X25519 ECDH:

```text
secret     = ECDH(operator_private, agent_public)
shared_key = HKDF(SHA-256, secret, info="rklings-aead")
agent_id   = hex(HKDF(SHA-256, secret, info="rklings-id")[:8])
```

The agent sends its 32-byte X25519 public key in the hello frame.
The server holds the operator's private key and derives the shared key
on each connection via ECDH.

For this exercise, the check harness provides the operator private key
via the `RKLINGS_OPERATOR_KEY` environment variable (hex-encoded, 32 bytes).

## Workspace

```text
exercises/L22-decoy-routing
├── go.mod
├── go.sum
└── main.go
```

Complete the HTTP handler in `main.go`. The crypto primitives
(`sealAEAD`, `openAEAD`, `newGCM`), the key registry, the WebSocket
upgrader, and the decoy page are provided.

## TODOs

```text
TODO 1  main.go   Read the hello frame, extract 32-byte public key, derive key
TODO 2  main.go   Decrypt hello, enter authenticated session read loop
```

### TODO 1: Read the hello frame and derive the key

The WebSocket Accept and decoy routing are handled for you. Inside the
handler, read the first binary message:

1. The decoded bytes are `agent_pub (32 bytes) || ciphertext`;
   you need at least 32 + 12 + 16 bytes (pubkey + GCM nonce + GCM tag)
2. Extract the 32-byte X25519 public key
3. Derive the shared key with `deriveKeys(operatorKey, agentPub)`
4. If derivation fails, return (deferred CloseNow handles cleanup)

### TODO 2: Decrypt and enter the session

Call `openAEAD` with the derived key and the ciphertext (everything
after the first 32 bytes). If decryption fails, the request is not from a
real agent: return.

On success, the agent is authenticated.
Enter a read loop that reads and decrypts subsequent frames.

The critical design point: every failure path closes the connection
without revealing why:

- Non-WebSocket visitors see the decoy page
- Failed authentications see a silent close
- No error message, no 400, no 403

## Test it

Two terminals.

### 1. Start the server

```console
$ ./rklings serve L22
```

### 2. Boot the target

```console
$ ./rklings run L22
```

### Test the decoy route

From a third terminal, any HTTP request returns the decoy page:

```console
$ curl -s http://127.0.0.1:8080/ | head -1
<!DOCTYPE html>
$ curl -s http://127.0.0.1:8080/admin | head -1
<!DOCTYPE html>
$ curl -s -X POST http://127.0.0.1:8080/ | head -1
<!DOCTYPE html>
```

Every path, every method, same page. No 404, no 403, no fingerprint.

### Test the agent route

The agent in the VM connects via WebSocket with a valid AEAD hello.
The server log confirms the authenticated session.
Open `http://127.0.0.1:8080` in a browser to see the decoy page
an observer would see.

## Validate

```console
$ ./rklings validate L22
  [+] GET request receives decoy page
  [+] POST with bad payload receives decoy page
  [+] WebSocket with valid agent crypto reaches C2
  PASS
```

## Next

The server now looks ordinary to any observer, but the target still
shows an unfamiliar process with predictable reconnect timing.
L23 changes those visible process signals and prevents useful core dumps.
