# L22: Decoy Routing

## Mission

L21 removed the agent's directory entry, but the server still answers every
connection the same way.
This exercise uses a client certificate to separate the operator protocol from
the decoy HTTP response on one TLS listener.

## What you will observe

Both clients complete TLS on the same port, but only the course certificate
reaches the command route:

```console
$ curl -k https://127.0.0.1:443/
<html><title>Welcome</title>...
$ curl -k --cert agent.crt --key agent.key https://127.0.0.1:443/
{"status":"c2","commands":["PING"]}
```

## Workspace

```text
exercises/L22-decoy-routing
├── go.mod
└── main.go
```

The runner supplies the CA, server certificate, and test client certificates.
Complete the TLS configuration and request routing in `main.go`.

## Ask for, but do not require, a client certificate

TLS supports mutual authentication.
The server can ask clients to present a certificate.
Go's `tls.Config` has four `ClientAuth` modes:

```text
NoClientCert             server never asks
RequestClientCert        server asks, client may decline
RequireAnyClientCert     server demands a cert, any cert
RequireAndVerifyClientCert  server demands a cert signed by ClientCAs
```

`RequireAndVerifyClientCert` rejects clients at the TLS layer.
The connection drops before your code runs.
You cannot serve a decoy page because there is no HTTP to respond on.

`RequestClientCert` is the key.
The server sends a CertificateRequest message.
Browsers silently respond with an empty certificate list.
TLS 1.3 encrypts the entire handshake after ServerHello, so a network observer
cannot see the CertificateRequest or the client's response.

Three outcomes from the same port:

```text
client connects
      |
      v
TLS handshake requests a certificate
      |
      +-- none presented --------------> decoy page
      +-- certificate fails verification -> decoy page
      +-- course certificate verifies -----> command route
```

## Route after the handshake

With `RequestClientCert`, Go does not verify the client cert automatically.
You must check it yourself in the HTTP handler:

```go
certs := r.TLS.PeerCertificates
if len(certs) == 0 {
    serveDecoy(w, r)
    return
}
```

When a cert is present, verify it against the rklings CA pool:

```go
opts := x509.VerifyOptions{
    Roots: rklingsCAPool,
}
_, err := certs[0].Verify(opts)
if err != nil {
    serveDecoy(w, r)
    return
}
serveC2(w, r)
```

## Implement it

Complete `main.go`:

- Load the rklings CA certificate into an `x509.CertPool`
- Configure `tls.Config` with `ClientAuth: tls.RequestClientCert`
- HTTP handler that inspects `r.TLS.PeerCertificates`
- Three-way routing: no cert, wrong cert, right cert
- Decoy handler serves a plausible static page
- C2 handler responds to agent commands

## Build the verifier

```go
import (
    "crypto/tls"
    "crypto/x509"
    "net/http"
    "os"
)

// Load CA cert
caPEM, _ := os.ReadFile("ca.crt")
pool := x509.NewCertPool()
pool.AppendCertsFromPEM(caPEM)

// TLS config
cfg := &tls.Config{
    ClientAuth: tls.RequestClientCert,
    // ClientCAs is NOT set: we verify manually
}

// In handler
r.TLS.PeerCertificates  // []*x509.Certificate, may be empty

opts := x509.VerifyOptions{Roots: pool}
opts.KeyUsages = []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth}
_, err := certs[0].Verify(opts)
```

## Test it

```sh
./rklings serve L22
```

The command builds your server and supplies the course certificates.
Open the decoy route in a browser:

```sh
https://127.0.0.1:8443/
```

Test both routes from another terminal:

```sh
curl -k https://127.0.0.1:8443/
<html><title>EduSigh</title>...           # decoy

curl -k --cert .rklings/certs/agent.crt \
  --key .rklings/certs/agent.key \
  https://127.0.0.1:8443/
{"status":"c2","commands":["PING"]}      # C2
```

Use `./rklings run L22` only when you want an isolated guest copy of the server.

## Validate

```sh
./rklings validate L22
  [+] server starts with RequestClientCert
  [+] no cert: receives decoy page
  [+] wrong cert: receives decoy page
  [+] rklings CA cert: receives C2 response
  PASS
```

## Next

The server now looks ordinary to an unauthenticated visitor, but the target
still shows an unfamiliar process with predictable callback timing.
L23 changes those visible process signals and prevents useful core dumps.
