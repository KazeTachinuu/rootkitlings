package main

import (
	"crypto/tls"
	"crypto/x509"
	"fmt"
	"log"
	"net"
	"os"
	"sync"
)

// agent holds the active agent connection and a mutex for safe access.
var agent struct {
	sync.Mutex
	conn net.Conn
}

func main() {
	if len(os.Args) < 4 {
		fmt.Fprintf(os.Stderr, "usage: %s <cert> <key> <ca>\n", os.Args[0])
		os.Exit(1)
	}
	certFile := os.Args[1]
	keyFile := os.Args[2]
	caFile := os.Args[3]

	ln := listen(certFile, keyFile, caFile)
	defer ln.Close()

	// Start webapp on :8080 in background
	go startWeb()
	go startTUI()

	fmt.Println("[*] Listening on :8443 for agent connections")
	fmt.Println("[*] Webapp on http://127.0.0.1:8080")

	for {
		conn, err := ln.Accept()
		if err != nil {
			log.Printf("accept: %v", err)
			continue
		}

		tlsConn := conn.(*tls.Conn)
		if err := tlsConn.Handshake(); err != nil {
			log.Printf("handshake: %v", err)
			conn.Close()
			continue
		}

		// Verify client presented a certificate
		state := tlsConn.ConnectionState()
		if len(state.PeerCertificates) == 0 {
			log.Println("no client certificate, rejecting")
			conn.Close()
			continue
		}

		agentID := state.PeerCertificates[0].Subject.CommonName
		fmt.Printf("[+] Agent connected: %s\n", agentID)

		agent.Lock()
		if agent.conn != nil {
			agent.conn.Close()
		}
		agent.conn = conn
		agent.Unlock()

	}
}

// listen creates a TLS listener with mutual authentication.
func listen(certFile, keyFile, caFile string) net.Listener {
	// TODO: load server certificate from certFile and keyFile
	// TODO: load CA cert from caFile into a CertPool for client verification
	// TODO: create tls.Config with RequireAndVerifyClientCert, MinVersion TLS 1.3
	// TODO: tls.Listen on :8443
	_ = certFile
	_ = keyFile
	_ = caFile

	log.Fatal("listen: not implemented")
	return nil
}

// sendToAgent sends a command to the agent and returns the response.
func sendToAgent(cmd string) (string, error) {
	agent.Lock()
	conn := agent.conn
	agent.Unlock()

	if conn == nil {
		return "", fmt.Errorf("no agent connected")
	}

	// TODO: write cmd + newline to conn
	// TODO: read response line(s) from conn
	// TODO: for SHELL responses, read until "." terminator
	// TODO: for DOWNLOAD responses, read size line + raw bytes

	return "", fmt.Errorf("not implemented")
}

// Ensure imports compile.
var _ = x509.NewCertPool
