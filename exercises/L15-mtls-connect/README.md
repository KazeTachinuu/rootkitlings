# L15: mTLS Callback

L14 showed the kernel launching a userspace binary.
Now you build that binary: a Go agent that phones home over mTLS.

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
- `client.crt`: the agent's own certificate (signed by rklings CA)
- `client.key`: the agent's private key

## go:embed

Go's `embed` directive bakes files into the binary at compile time.
No file I/O at runtime. The binary IS the credential.

```go
import _ "embed"

//go:embed certs/ca.crt
var caCert []byte

//go:embed certs/client.crt
var clientCert []byte

//go:embed certs/client.key
var clientKey []byte
```

The `//go:embed` comment must appear directly above the variable.
No blank line between the directive and the var declaration.

## tls.Config

The agent's TLS configuration needs three things:
1. A certificate pool containing the CA cert (to verify the server).
2. The client's own certificate pair (to present to the server).
3. TLS 1.3 minimum (the only version worth using).

```go
pool := x509.NewCertPool()
pool.AppendCertsFromPEM(caCert)

cert, err := tls.X509KeyPair(clientCert, clientKey)

cfg := &tls.Config{
    RootCAs:      pool,
    Certificates: []tls.Certificate{cert},
    MinVersion:   tls.VersionTLS13,
}
```

## The connection

The agent connects to the server at 10.0.2.2:443.
That is the QEMU default gateway: the host machine from the VM's perspective.

```go
conn, err := tls.Dial("tcp", "10.0.2.2:443", cfg)
```

After connecting, the agent reads one line from the server and prints it.
Then it closes the connection and exits.
L17 adds a command loop.

## You write

`main.go` (~60 lines):

- go:embed variables for ca.crt, client.crt, client.key
- Build tls.Config with RootCAs, Certificates, MinVersion
- tls.Dial to 10.0.2.2:443
- Read one line, print it, close

## API

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
cert, err := tls.X509KeyPair(clientCert, clientKey)

// TLS config
cfg := &tls.Config{
    RootCAs:      pool,
    Certificates: []tls.Certificate{cert},
    MinVersion:   tls.VersionTLS13,
}

// Connect
conn, err := tls.Dial("tcp", "10.0.2.2:443", cfg)
defer conn.Close()
```

## Test it

```
$ rklings run l15
```

The lab server starts on the host, listening on :443 with mTLS.
The agent binary is built with embedded certs and copied to the VM.

```
root@vm:~# ./agent
connected to 10.0.2.2:443
server: WELCOME
```

The lab server's web panel at http://localhost:8080 shows the connection:

```
[+] TLS 1.3 connection from 10.0.2.2
[+] Client cert: CN=agent-abc123, issuer=rklings CA
```

## Validate

```
$ rklings validate l15
  [+] agent binary builds
  [+] agent connects to mTLS server
  [+] TLS 1.3 negotiated
  [+] client certificate verified by server
  PASS
```

## Next

The agent connects.
L16 makes it useful: translating server commands into mkdir calls
that the kernel module understands.
