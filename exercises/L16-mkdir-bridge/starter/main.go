// Agent mkdir bridge.
// Connect to the server over mTLS, read commands, translate each
// to a mkdir name, and issue os.Mkdir so the kernel module acts on it.
package main

import (
	"bufio"
	"crypto/tls"
	"crypto/x509"
	_ "embed"
	"fmt"
	"os"
	"strings"
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
	// mTLS setup (same as L15)
	pool := x509.NewCertPool()
	if !pool.AppendCertsFromPEM(caCert) {
		return fmt.Errorf("failed to parse CA cert")
	}
	cert, err := tls.X509KeyPair(clientCert, clientKey)
	if err != nil {
		return fmt.Errorf("client keypair: %w", err)
	}
	cfg := &tls.Config{
		RootCAs:      pool,
		Certificates: []tls.Certificate{cert},
		MinVersion:   tls.VersionTLS13,
	}

	conn, err := tls.Dial("tcp", serverAddr, cfg)
	if err != nil {
		return fmt.Errorf("dial: %w", err)
	}
	defer conn.Close()

	fmt.Println("connected to", serverAddr)

	scanner := bufio.NewScanner(conn)
	for scanner.Scan() {
		line := scanner.Text()
		if line == "" {
			continue
		}
		name := translate(line)
		if name == "" {
			fmt.Fprintf(os.Stderr, "unknown command: %s\n", line)
			continue
		}
		os.Mkdir(name, 0)
		fmt.Printf("exec: %s -> %s\n", line, name)
		if name == "rklings.quit" {
			break
		}
	}
	return scanner.Err()
}

// TODO: translate server command to mkdir name
func translate(line string) string {
	_ = strings.SplitN(line, " ", 3)
	return ""
}
