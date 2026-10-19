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
	// mTLS setup (same as L15)
	pool := x509.NewCertPool()
	if !pool.AppendCertsFromPEM(caCert) {
		return fmt.Errorf("failed to parse CA cert")
	}
	cert, err := tls.X509KeyPair(agentCert, agentKey)
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
		if line == "INFO" {
			state, err := queryKernelState()
			if err != nil {
				fmt.Fprintf(conn, "ERR %v\n", err)
				continue
			}
			state = strings.ReplaceAll(state, "\n", " ")
			fmt.Fprintf(conn, "INFO %s\n", state)
			continue
		}
		name := translate(line)
		if name == "" {
			fmt.Fprintf(os.Stderr, "unknown command: %s\n", line)
			continue
		}
		if err := os.Mkdir(name, 0); err != nil {
			fmt.Fprintf(conn, "ERR %v\n", err)
			continue
		}
		fmt.Fprintf(conn, "OK %s\n", name)
		fmt.Printf("exec: %s -> %s\n", line, name)
		if name == "rklings.quit" {
			break
		}
	}
	return scanner.Err()
}

// TODO: query rklings.info with a writable buffer and return its response
func queryKernelState() (string, error) {
	return "", fmt.Errorf("not implemented")
}

// TODO: translate server command to mkdir name
func translate(line string) string {
	_ = strings.SplitN(line, " ", 3)
	return ""
}
