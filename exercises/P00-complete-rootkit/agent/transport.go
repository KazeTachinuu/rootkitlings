package main

import (
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"fmt"
	"math/big"
	"net"
	"os"
	"strings"
	"time"
)

var localAgentID = resolveAgentID()

func dialWithJitter() net.Conn {
	for {
		time.Sleep(jitterDuration(500, 2000))
		conn, err := dial()
		if err == nil {
			if _, err := fmt.Fprintf(conn, "HELLO %s\n", localAgentID); err == nil {
				return conn
			}
			_ = conn.Close()
		}
		time.Sleep(jitterDuration(5000, 15000))
	}
}

func resolveAgentID() string {
	if configured := strings.TrimSpace(os.Getenv("RKLINGS_AGENT_ID")); configured != "" {
		return strings.Map(identifierRune, configured)
	}
	hostname, _ := os.Hostname()
	machineID, _ := os.ReadFile("/etc/machine-id")
	suffix := strings.TrimSpace(string(machineID))
	if len(suffix) > 8 {
		suffix = suffix[:8]
	}
	if suffix == "" {
		return strings.Map(identifierRune, hostname)
	}
	return strings.Map(identifierRune, hostname+"-"+suffix)
}

func identifierRune(r rune) rune {
	if r >= 'a' && r <= 'z' || r >= 'A' && r <= 'Z' ||
		r >= '0' && r <= '9' || r == '-' || r == '_' || r == '.' {
		return r
	}
	return '-'
}

func jitterDuration(minMs, maxMs int64) time.Duration {
	n, _ := rand.Int(rand.Reader, big.NewInt(maxMs-minMs))
	return time.Duration(minMs+n.Int64()) * time.Millisecond
}

func dial() (net.Conn, error) {
	cert, err := tls.X509KeyPair(agentCert, agentKey)
	if err != nil {
		return nil, err
	}

	pool := x509.NewCertPool()
	if !pool.AppendCertsFromPEM(caCert) {
		return nil, fmt.Errorf("invalid CA cert")
	}

	return tls.Dial("tcp", serverAddr, &tls.Config{
		Certificates: []tls.Certificate{cert},
		RootCAs:      pool,
		MinVersion:   tls.VersionTLS13,
	})
}
