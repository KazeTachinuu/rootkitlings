// Agent mTLS callback.
// Connect to the server at 10.0.2.2:443 using mutual TLS.
// Embed the CA cert, client cert, and client key at compile time.
// Read one line from the server, print it, and exit.
package main

import (
	"bufio"
	"crypto/tls"
	"crypto/x509"
	_ "embed"
	"fmt"
	"os"
)

//go:embed certs/ca.crt
var caCert []byte

//go:embed certs/client.crt
var clientCert []byte

//go:embed certs/client.key
var clientKey []byte

const serverAddr = "10.0.2.2:443"

func main() {
	if err := run(); err != nil {
		fmt.Fprintf(os.Stderr, "agent: %v\n", err)
		os.Exit(1)
	}
}

func run() error {
	// TODO: build x509.CertPool from caCert, parse client key pair

	// TODO: build tls.Config with RootCAs, Certificates, MinVersion TLS 1.3

	// TODO: tls.Dial to serverAddr

	// TODO: read one line with bufio.Scanner, print it

	_ = bufio.NewScanner(nil) // remove after implementing
	_ = tls.Config{}
	_ = x509.NewCertPool()
	return nil
}
