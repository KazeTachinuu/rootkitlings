// Agent mTLS callback.
// Connect to the rootless lesson server at 10.0.2.2:8443 using mutual TLS.
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
	"time"
)

//go:embed certs/ca.crt
var caCert []byte

//go:embed certs/agent.crt
var agentCert []byte

//go:embed certs/agent.key
var agentKey []byte

var serverAddr = envOr("RKLINGS_SERVER_ADDR", "10.0.2.2:8443")

func envOr(name, fallback string) string {
	if value := os.Getenv(name); value != "" {
		return value
	}
	return fallback
}

func main() {
	if err := run(); err != nil {
		fmt.Fprintf(os.Stderr, "agent: %v\n", err)
		os.Exit(1)
	}
}

func run() error {
	pool := x509.NewCertPool()
	if !pool.AppendCertsFromPEM(caCert) {
		return fmt.Errorf("failed to parse CA certificate")
	}

	cert, err := tls.X509KeyPair(agentCert, agentKey)
	if err != nil {
		return fmt.Errorf("failed to parse client key pair: %w", err)
	}

	cfg := &tls.Config{
		RootCAs:      pool,
		Certificates: []tls.Certificate{cert},
		MinVersion:   tls.VersionTLS13,
	}

	var conn *tls.Conn
	for {
		conn, err = tls.Dial("tcp", serverAddr, cfg)
		if err == nil {
			break
		}
		time.Sleep(2 * time.Second)
	}
	defer conn.Close()

	scanner := bufio.NewScanner(conn)
	if scanner.Scan() {
		fmt.Println(scanner.Text())
	}
	if err := scanner.Err(); err != nil {
		return fmt.Errorf("read error: %w", err)
	}

	return nil
}
