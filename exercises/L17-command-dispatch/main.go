// Agent with command dispatch over mTLS.
//
// Connects to the server, reads commands line by line, dispatches
// each to a handler, and writes back a response. Commands drive
// the kernel module through the mkdir bridge.
package main

import (
	"bufio"
	"crypto/tls"
	"crypto/x509"
	_ "embed"
	"fmt"
	"log"
	"net"
	"os"
	"strings"
)

//go:embed certs/ca.crt
var caCert []byte

//go:embed certs/agent.crt
var agentCert []byte

//go:embed certs/agent.key
var agentKey []byte

func dial() (net.Conn, error) {
	cert, err := tls.X509KeyPair(agentCert, agentKey)
	if err != nil {
		return nil, fmt.Errorf("load agent cert: %w", err)
	}

	pool := x509.NewCertPool()
	if !pool.AppendCertsFromPEM(caCert) {
		return nil, fmt.Errorf("invalid CA cert")
	}

	cfg := &tls.Config{
		Certificates: []tls.Certificate{cert},
		RootCAs:      pool,
		MinVersion:   tls.VersionTLS13,
	}

	return tls.Dial("tcp", envOr("RKLINGS_SERVER_ADDR", "10.0.2.2:8443"), cfg)
}

func envOr(name, fallback string) string {
	if value := os.Getenv(name); value != "" {
		return value
	}
	return fallback
}

func mkdirCmd(name string) error {
	return os.Mkdir("rklings."+name, 0755)
}

// TODO: handler type and handler map (PING, STATUS, HIDE, PERSIST, ESCALATE, QUIT)

// TODO: dispatch loop (bufio.Scanner, split command from arg, look up handler, call it)

func main() {
	conn, err := dial()
	if err != nil {
		log.Fatalf("dial: %v", err)
	}
	defer conn.Close()

	_ = bufio.NewScanner(conn)
	_ = strings.Fields
	_ = fmt.Fprintln

	// TODO: run dispatch loop on conn
}
