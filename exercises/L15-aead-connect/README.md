# L15: AEAD Connect

## Mission

Build the first userspace agent. It must:

1. Derive a shared encryption key
2. Open a WebSocket connection to the server
3. Send an authenticated hello frame
4. Decrypt the server's first message
5. Exit

L14 showed the kernel launching a userspace binary. G00 already proved that you
can build a Go process, handle errors, and exchange data over a network.

Now add cryptography: a Go agent that phones home over an encrypted WebSocket.

## What you will observe

The agent:

1. Derives a shared key from embedded X25519 keys.
2. Opens a WebSocket connection to the operator.
3. Encrypts a hello frame with AES-256-GCM and sends it as the first binary
   message.
4. Receives an encrypted response, decrypts it, and prints the result.

## Workspace

```text
exercises/L15-aead-connect
├── go.mod
└── main.go
```

Only `main.go` is editable. `rklings build` injects the generated key files into
the temporary build context, so key files do not appear beside your source.

## X25519 ECDH Key Agreement

The agent and server share a symmetric key without any runtime handshake:

- Each side has its own X25519 private key and the other side's public key,
  embedded at compile time
- The ECDH operation produces the same shared secret on both sides
- Hashing that secret with SHA-256 gives a 32-byte AES key

```text
  Agent                              Server
  agent.key (private)                operator.key (private)
  operator.pub (public)              agent.pub (public)
       \                                /
        ECDH(agent.key, operator.pub)  ECDH(operator.key, agent.pub)
                     \                /
                   same shared secret
                          |
                      SHA-256
                          |
                    32-byte AES key
```

No certificate authority, no handshake messages, no session negotiation. The
keys are baked into the binary; the shared key is computed once at startup.

The agent's 32-byte X25519 public key is sent in the hello frame. The server
uses it to derive the shared key and identify which agent is calling.

## AES-256-GCM Authenticated Encryption

AES-GCM encrypts and authenticates in one pass. Given a 32-byte key and a
12-byte random nonce, `Seal` produces ciphertext that is both confidential and
tamper-proof. `Open` rejects anything that was modified in transit.

The wire format prepends the nonce to the ciphertext so the receiver can split
them apart:

```text
nonce (12 bytes) || ciphertext+tag
```

## WebSocket as Transport

The agent opens a WebSocket connection to the operator server. The first binary
frame is the hello message, which authenticates the agent:

```text
agent_pub (32 bytes) || AES_GCM_Seal({"t":"hello"})
```

The server extracts the 32-byte public key, derives the shared key via ECDH, and
decrypts the rest. If decryption succeeds, the agent is authenticated and the
connection stays open for bidirectional encrypted frames.

After the hello, every frame in both directions is a standalone AEAD ciphertext
(nonce + sealed payload). No form encoding, no base64 wrapping, just raw binary
WebSocket frames.

If the public key is invalid or decryption fails, the server closes the
connection silently.

## go:embed

Go's `embed` directive bakes files into the binary at compile time. No file I/O
at runtime. The binary IS the credential.

```go
import _ "embed"

//go:embed keys/agent.key
var agentPrivKey []byte

//go:embed keys/operator.pub
var operatorPubKey []byte
```

The `//go:embed` comment must appear directly above the variable. No blank line
between the directive and the var declaration.

## TODOs

```text
TODO 1  main.go   Derive the shared key (X25519 ECDH + SHA-256)
TODO 2  main.go   Build AES-256-GCM cipher from shared key
TODO 3  main.go   Connect via WebSocket and send the hello frame
TODO 4  main.go   Read one server message and decrypt it
```

### TODO 1 -- Derive the shared key

Use `crypto/ecdh` to parse both keys and perform ECDH:

```go
curve := ecdh.X25519()
priv, err := curve.NewPrivateKey(agentPrivKey)
pub, err := curve.NewPublicKey(operatorPubKey)
secret, err := priv.ECDH(pub)
```

Derive the encryption key with HKDF:

```go
io.ReadFull(hkdf.New(sha256.New, secret, nil, []byte("rklings-aead")), sharedKey[:])
```

The agent's 32-byte X25519 public key (`priv.PublicKey().Bytes()`) is sent in
the hello frame so the server can derive the same shared key.

### TODO 2 -- Build AES-256-GCM cipher

Create an AES block cipher from the 32-byte key, then wrap it in GCM:

```go
block, err := aes.NewCipher(sharedKey[:])
gcm, err := cipher.NewGCM(block)
```

### TODO 3 -- Connect via WebSocket and send the hello frame

Dial the operator with `websocket.Dial(ctx, "ws://<serverAddr>/", nil)`.

1. Marshal a JSON hello message: `{"t":"hello"}`
2. Generate a random nonce, seal the plaintext with GCM
3. Prepend the 32-byte agent public key to produce the hello frame:

```go
ct := gcm.Seal(nonce, nonce, plain, nil)
frame := append(agentPubBytes, ct...)
conn.Write(ctx, websocket.MessageBinary, frame)
```

### TODO 4 -- Read one server message and decrypt it

Call `conn.Read(ctx)` to receive the server's first binary frame. Split the
message into nonce and ciphertext, call `gcm.Open`, and print the decrypted
JSON.

Remove the placeholder lines at the bottom of `run()` once the real
implementation is in place.

## Test it

In one host terminal, start the lab server:

```console
$ ./rklings serve L15
```

In another, build and boot the target:

```console
$ ./rklings run L15
```

`run` builds the agent with embedded keys, stages it in the VM, and starts it in
the background. Open `http://127.0.0.1:8080`.

The panel reports live values from the agent connection:

- encrypted hello received and decrypted
- agent public key recognized
- response encrypted and sent

Stop `run` and `serve` with `Ctrl+C` when finished.

## Validate

```console
$ ./rklings validate L15
  [+] Agent sends encrypted hello to server
  [+] Server decrypts and responds
  PASS
```

## Next

The target and server now share an encrypted channel, but the agent sends one
hello and exits. L16 bridges server commands to the kernel through `os.Mkdir`,
giving the operator remote control over the rootkit.
