# L15: mTLS Callback

## Mission

Build the first userspace agent.
It must authenticate the server, present its own certificate, receive one real
message, and exit.

L14 showed the kernel launching a userspace binary.
G00 already proved that you can build a Go process, handle errors, and exchange
framed data over a TCP stream.
Now keep that operational model and add identity: a Go agent that phones home
over mTLS.

## What you will observe

The agent rejects an unauthenticated server, presents its embedded client
certificate to the course server, completes TLS 1.3, receives the framed
message, and exits.
The server records the verified client identity rather than trusting a name sent
inside the connection.

## Workspace

```text
exercises/L15-mtls-connect
├── go.mod
└── main.go
```

Only `main.go` is editable. `rklings build` injects the generated CA and agent
key pair into the temporary build context, so certificate files do not appear
beside your source.

## Mutual TLS

Normal TLS: the client verifies the server's certificate.
Mutual TLS: both sides verify each other.
The server checks the client's certificate against a private CA.
No valid cert, no connection.

```text
  Agent (VM)                          Server (host)
     |                                     |
     |------- ClientHello --------------->|
     |<------ ServerHello + ServerCert ---|
     |<------ CertificateRequest ---------|
     |------- ClientCert + Verify ------->|
     |                                     |
     |  server checks client cert         |
     |  against rklings CA                |
     |                                     |
     |<====== encrypted channel =========>|
```

The agent embeds three files at compile time:

- `ca.crt`: the rklings CA certificate (to verify the server)
- `agent.crt`: the agent's own certificate (signed by rklings CA)
- `agent.key`: the agent's private key

## go:embed

Go's `embed` directive bakes files into the binary at compile time.
No file I/O at runtime.
The binary IS the credential.

```go
import _ "embed"

//go:embed certs/ca.crt
var caCert []byte

//go:embed certs/agent.crt
var agentCert []byte

//go:embed certs/agent.key
var agentKey []byte
```

The `//go:embed` comment must appear directly above the variable.
No blank line between the directive and the var declaration.

## tls.Config

The agent's TLS configuration needs three things:

1. A certificate pool containing the CA cert (to verify the server).
2. The client's own certificate pair (to present to the server).
3. TLS 1.3 as the minimum accepted version.

```go
pool := x509.NewCertPool()
pool.AppendCertsFromPEM(caCert)

cert, err := tls.X509KeyPair(agentCert, agentKey)

cfg := &tls.Config{
    RootCAs:      pool,
    Certificates: []tls.Certificate{cert},
    MinVersion:   tls.VersionTLS13,
}
```

## The connection

The agent connects to the rootless lab server at `10.0.2.2:8443`.
That is the QEMU default gateway: the host machine from the VM's perspective.

```go
conn, err := tls.Dial("tcp", "10.0.2.2:8443", cfg)
```

If the server is not running yet, the reference pattern retries instead of
exiting permanently.
After connecting, the agent reads one line from the server and prints it.
Then it closes the connection and exits.
L17 adds a command loop.

## Implement it

Complete `main.go`:

- go:embed variables for ca.crt, agent.crt, agent.key
- Build tls.Config with RootCAs, Certificates, MinVersion
- tls.Dial to 10.0.2.2:8443
- Read one line, print it, close

### Interfaces used here

```go
import (
    "crypto/tls"
    "crypto/x509"
    _ "embed"
)

// Embed files
//go:embed certs/ca.crt
var caCert []byte

// Certificate pool (verify server)
pool := x509.NewCertPool()
ok := pool.AppendCertsFromPEM(caCert)

// Client certificate (present to server)
cert, err := tls.X509KeyPair(agentCert, agentKey)

// TLS config
cfg := &tls.Config{
    RootCAs:      pool,
    Certificates: []tls.Certificate{cert},
    MinVersion:   tls.VersionTLS13,
}

// Connect
conn, err := tls.Dial("tcp", "10.0.2.2:8443", cfg)
defer conn.Close()
```

## Test it

In one host terminal, start the mTLS lab server:

```sh
./rklings serve L15
```

In another, build and boot the target:

```sh
./rklings run L15
```

`run` builds the agent with embedded credentials, stages it in the VM, and
starts it in the background.
Open `http://127.0.0.1:8080`.
The panel reports live values from the agent connection:

- connection state and peer address
- TLS version and cipher suite
- verified client certificate name

Stop `run` and `serve` with `Ctrl+C` when finished.

## Validate

```sh
./rklings validate L15
  [+] agent binary builds
  [+] agent connects to mTLS server
  [+] TLS 1.3 negotiated
  [+] client certificate verified by server
  PASS
```

## Next

The target and server now have an authenticated connection.
L16 uses that connection to trigger the kernel controls remotely.
