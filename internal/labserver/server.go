package labserver

import (
	"context"
	"crypto/aes"
	"crypto/cipher"
	"crypto/ecdh"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"strings"
	"sync"
	"time"

	"github.com/coder/websocket"
	"golang.org/x/crypto/hkdf"
)

type Config struct {
	ExerciseID    string
	Title         string
	KeyDir        string
	Addr          string
	HTTPAddr      string
	Code          string
	Commands      []string
	Transfer      bool
	HandshakeOnly bool
	Welcome       string
}

type Event struct {
	Time      string `json:"time"`
	Direction string `json:"direction"`
	Message   string `json:"message"`
}

type command struct {
	ID   string          `json:"id"`
	Op   string          `json:"op"`
	Arg  string          `json:"arg,omitempty"`
	Data json.RawMessage `json:"data,omitempty"`
}
type result struct {
	ID   string `json:"id"`
	Data any    `json:"data"`
}

type pendingCmd struct {
	cmd  command
	done chan result
}

type Server struct {
	cfg          Config
	mu           sync.Mutex
	operatorPriv *ecdh.PrivateKey
	agentID      string
	sharedKey    [32]byte
	gcm          cipher.AEAD
	connected    bool
	seen         bool
	events       []Event
	queue        []pendingCmd
	notify       chan struct{}
	agentAddr    net.Listener
	httpServer   *http.Server
	httpLn       net.Listener

	decoyHTML []byte

	wsConn    *websocket.Conn
	wsWriteMu sync.Mutex
	wsRespCh  chan result

	dlMu sync.RWMutex
	dlCh chan []byte

	ptyBuf    []byte
	connTimes []int64
}

func (s *Server) AgentAddr() string {
	_, port, err := net.SplitHostPort(s.agentAddr.Addr().String())
	if err != nil {
		return "10.0.2.2:8443"
	}
	return net.JoinHostPort("10.0.2.2", port)
}

func (s *Server) RelayURL() string {
	_, port, err := net.SplitHostPort(s.httpLn.Addr().String())
	if err != nil {
		return "http://10.0.2.2:4444"
	}
	return "http://" + net.JoinHostPort("10.0.2.2", port)
}

func deriveKeys(operatorPriv *ecdh.PrivateKey, agentPubBytes []byte) (agentID string, shared [32]byte, gcm cipher.AEAD, err error) {
	curve := ecdh.X25519()
	agentPub, err := curve.NewPublicKey(agentPubBytes)
	if err != nil {
		return "", shared, nil, fmt.Errorf("parse agent pub: %w", err)
	}
	secret, err := operatorPriv.ECDH(agentPub)
	if err != nil {
		return "", shared, nil, fmt.Errorf("ECDH: %w", err)
	}
	if _, err := io.ReadFull(hkdf.New(sha256.New, secret, nil, []byte("rklings-aead")), shared[:]); err != nil {
		return "", shared, nil, fmt.Errorf("derive key: %w", err)
	}
	var idBytes [8]byte
	if _, err := io.ReadFull(hkdf.New(sha256.New, secret, nil, []byte("rklings-id")), idBytes[:]); err != nil {
		return "", shared, nil, fmt.Errorf("derive id: %w", err)
	}
	agentID = hex.EncodeToString(idBytes[:])

	block, err := aes.NewCipher(shared[:])
	if err != nil {
		return "", shared, nil, fmt.Errorf("AES: %w", err)
	}
	gcm, err = cipher.NewGCM(block)
	if err != nil {
		return "", shared, nil, fmt.Errorf("GCM: %w", err)
	}
	return agentID, shared, gcm, nil
}

func Start(cfg Config) (*Server, error) {
	if cfg.Addr == "" {
		cfg.Addr = ":8443"
	}
	if cfg.HTTPAddr == "" {
		cfg.HTTPAddr = "127.0.0.1:8080"
	}

	operatorKeyBytes, err := os.ReadFile(cfg.KeyDir + "/operator.key")
	if err != nil {
		return nil, fmt.Errorf("read operator.key: %w", err)
	}
	agentPubBytes, err := os.ReadFile(cfg.KeyDir + "/agent.pub")
	if err != nil {
		return nil, fmt.Errorf("read agent.pub: %w", err)
	}

	curve := ecdh.X25519()
	operatorPriv, err := curve.NewPrivateKey(operatorKeyBytes)
	if err != nil {
		return nil, fmt.Errorf("parse operator key: %w", err)
	}

	// Pre-derive agentID for display; actual key derivation happens per-connection
	// from the pubkey in the hello frame.
	agentID, shared, gcm, err := deriveKeys(operatorPriv, agentPubBytes)
	if err != nil {
		return nil, err
	}

	s := &Server{
		cfg:          cfg,
		operatorPriv: operatorPriv,
		agentID:      agentID,
		sharedKey:    shared,
		gcm:          gcm,
		notify:       make(chan struct{}, 1),
		decoyHTML:    []byte(`<!doctype html><html><head><title>Welcome</title></head><body><h1>Welcome</h1><p>Nothing to see here.</p></body></html>`),
		wsRespCh:     make(chan result, 16),
	}

	agentMux := http.NewServeMux()
	agentMux.HandleFunc("/", s.agentHandler)
	agentLn, err := net.Listen("tcp", cfg.Addr)
	if err != nil {
		return nil, fmt.Errorf("listen %s: %w", cfg.Addr, err)
	}
	s.agentAddr = agentLn
	agentSrv := &http.Server{Handler: agentMux, ReadHeaderTimeout: 5 * time.Second}
	go agentSrv.Serve(agentLn)

	webMux := http.NewServeMux()
	webMux.HandleFunc("/", s.page)
	webMux.HandleFunc("/api/state", s.state)
	webMux.HandleFunc("/api/command", s.webCommand)
	webMux.HandleFunc("/api/upload", s.upload)
	webMux.HandleFunc("/wait", s.relayWait)
	webMux.HandleFunc("/cmd", s.relayCommand)
	webMux.HandleFunc("/download", s.relayDownload)
	webMux.HandleFunc("/download-size", s.relayDownloadSize)
	webMux.HandleFunc("/upload", s.relayUpload)
	webMux.HandleFunc("/connections", s.relayConnections)
	webMux.HandleFunc("/pty", s.relayPTY)
	s.httpServer = &http.Server{Addr: cfg.HTTPAddr, Handler: webMux, ReadHeaderTimeout: 5 * time.Second}
	httpLn, err := net.Listen("tcp", cfg.HTTPAddr)
	if err != nil {
		agentLn.Close()
		return nil, fmt.Errorf("listen %s: %w", cfg.HTTPAddr, err)
	}
	s.httpLn = httpLn
	go s.httpServer.Serve(httpLn)
	return s, nil
}

func (s *Server) Close() error {
	_ = s.agentAddr.Close()
	return s.httpServer.Close()
}

func (s *Server) agentHandler(w http.ResponseWriter, r *http.Request) {
	if r.Header.Get("Upgrade") != "" {
		s.handleAgentWS(w, r)
		return
	}
	s.serveDecoy(w)
}

func (s *Server) handleAgentWS(w http.ResponseWriter, r *http.Request) {
	conn, err := websocket.Accept(w, r, &websocket.AcceptOptions{
		OriginPatterns: []string{"*"},
	})
	if err != nil {
		return
	}
	defer conn.CloseNow()
	conn.SetReadLimit(4 << 20)
	ctx := r.Context()

	// Read hello frame: agent_pub(32) || AEAD({"t":"hello"})
	_, msg, err := conn.Read(ctx)
	if err != nil || len(msg) < 32+12+16 {
		return
	}
	agentPubBytes := msg[:32]
	agentID, _, gcm, err := deriveKeys(s.operatorPriv, agentPubBytes)
	if err != nil {
		return
	}
	if agentID != s.agentID {
		return
	}
	ct := msg[32:]
	pt, err := gcm.Open(nil, ct[:gcm.NonceSize()], ct[gcm.NonceSize():], nil)
	if err != nil {
		return
	}
	var hello struct{ T string `json:"t"` }
	if json.Unmarshal(pt, &hello) != nil || hello.T != "hello" {
		return
	}

	s.mu.Lock()
	s.connected = true
	s.seen = true
	s.wsConn = conn
	s.gcm = gcm
	s.agentID = agentID
	s.connTimes = append(s.connTimes, time.Now().UnixNano())
	s.addLocked("in", "agent connected (ws): "+agentID[:12])
	s.mu.Unlock()

	if s.cfg.HandshakeOnly {
		welcome := s.cfg.Welcome
		if welcome == "" {
			welcome = `{"status":"ok","message":"handshake complete"}`
		}
		s.wsSend(ctx, conn, gcm, []byte(welcome))
		s.mu.Lock()
		s.connected = false
		s.wsConn = nil
		s.mu.Unlock()
		conn.Close(websocket.StatusNormalClosure, "done")
		return
	}

	defer func() {
		s.mu.Lock()
		s.connected = false
		s.wsConn = nil
		s.mu.Unlock()
	}()

	// Read pump
	go func() {
		for {
			_, raw, err := conn.Read(ctx)
			if err != nil {
				close(s.wsRespCh)
				s.wsRespCh = make(chan result, 16)
				return
			}
			if len(raw) < gcm.NonceSize() {
				continue
			}
			pt, err := gcm.Open(nil, raw[:gcm.NonceSize()], raw[gcm.NonceSize():], nil)
			if err != nil {
				continue
			}

			// Chunked binary download frames
			if len(pt) > 0 && pt[0] == 0x01 {
				s.dlMu.RLock()
				ch := s.dlCh
				s.dlMu.RUnlock()
				if ch != nil {
					select {
					case ch <- append([]byte(nil), pt[1:]...):
					default:
					}
				}
				continue
			}
			if len(pt) >= 1 && pt[0] == 0x02 {
				s.dlMu.RLock()
				ch := s.dlCh
				s.dlMu.RUnlock()
				if ch != nil {
					close(ch)
					s.dlMu.Lock()
					s.dlCh = nil
					s.dlMu.Unlock()
				}
				continue
			}

			var envelope struct {
				Data json.RawMessage `json:"data"`
				PTY  string          `json:"pty"`
			}
			if json.Unmarshal(pt, &envelope) != nil {
				continue
			}
			if envelope.PTY != "" {
				decoded, _ := base64.StdEncoding.DecodeString(envelope.PTY)
				if len(decoded) > 0 {
					s.mu.Lock()
					s.ptyBuf = append(s.ptyBuf, decoded...)
					s.mu.Unlock()
				}
				continue
			}
			if envelope.Data != nil {
				s.wsRespCh <- result{Data: envelope.Data}
			}
		}
	}()

	// Command pump
	for {
		select {
		case <-s.notify:
		case <-time.After(30 * time.Second):
			s.wsSend(ctx, conn, gcm, []byte(`{"t":"ping"}`))
			continue
		}

		s.mu.Lock()
		var pending []pendingCmd
		pending = append(pending, s.queue...)
		s.mu.Unlock()

		for _, p := range pending {
			cmdJSON, _ := json.Marshal(p.cmd)
			if err := s.wsSend(ctx, conn, gcm, cmdJSON); err != nil {
				return
			}
			s.mu.Lock()
			s.addLocked("out", fmt.Sprintf("%s %s", p.cmd.Op, p.cmd.Arg))
			s.mu.Unlock()

			select {
			case res, ok := <-s.wsRespCh:
				if !ok {
					return
				}
				s.mu.Lock()
				for i, q := range s.queue {
					if q.cmd.ID == p.cmd.ID {
						q.done <- result{ID: p.cmd.ID, Data: res.Data}
						s.queue = append(s.queue[:i], s.queue[i+1:]...)
						s.addLocked("in", fmt.Sprintf("result for %s %s", p.cmd.Op, p.cmd.Arg))
						break
					}
				}
				s.mu.Unlock()
			case <-time.After(60 * time.Second):
				return
			}
		}
	}
}

func (s *Server) wsSend(ctx context.Context, conn *websocket.Conn, gcm cipher.AEAD, plaintext []byte) error {
	nonce := make([]byte, gcm.NonceSize())
	rand.Read(nonce)
	ct := gcm.Seal(nonce, nonce, plaintext, nil)
	s.wsWriteMu.Lock()
	defer s.wsWriteMu.Unlock()
	return conn.Write(ctx, websocket.MessageBinary, ct)
}

func (s *Server) serveDecoy(w http.ResponseWriter) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Write(s.decoyHTML)
}

func (s *Server) addLocked(direction, message string) {
	s.events = append(s.events, Event{Time: time.Now().Format("15:04:05"), Direction: direction, Message: message})
	if len(s.events) > 100 {
		s.events = s.events[len(s.events)-100:]
	}
}

var cmdSeq int64
var cmdSeqMu sync.Mutex

func nextCmdID() string {
	cmdSeqMu.Lock()
	defer cmdSeqMu.Unlock()
	cmdSeq++
	return fmt.Sprintf("lab-%d", cmdSeq)
}

func (s *Server) enqueue(op, arg string) (result, error) {
	id := nextCmdID()
	done := make(chan result, 1)
	s.mu.Lock()
	s.queue = append(s.queue, pendingCmd{cmd: command{ID: id, Op: op, Arg: arg}, done: done})
	s.addLocked("out", fmt.Sprintf("%s %s", op, arg))
	s.mu.Unlock()
	select {
	case s.notify <- struct{}{}:
	default:
	}

	select {
	case res := <-done:
		return res, nil
	case <-time.After(60 * time.Second):
		s.mu.Lock()
		for i, p := range s.queue {
			if p.cmd.ID == id {
				s.queue = append(s.queue[:i], s.queue[i+1:]...)
				break
			}
		}
		s.mu.Unlock()
		return result{}, fmt.Errorf("timeout waiting for agent response")
	}
}

func (s *Server) call(cmdStr string) (string, []byte, error) {
	s.mu.Lock()
	connected := s.connected
	s.mu.Unlock()
	if !connected {
		return "", nil, fmt.Errorf("agent is not connected")
	}

	parts := strings.SplitN(cmdStr, " ", 2)
	op := parts[0]
	arg := ""
	if len(parts) > 1 {
		arg = parts[1]
	}

	var dlCh chan []byte
	if op == "DOWNLOAD" {
		dlCh = make(chan []byte, 256)
		s.dlMu.Lock()
		s.dlCh = dlCh
		s.dlMu.Unlock()
	}

	res, err := s.enqueue(op, arg)
	if err != nil {
		return "", nil, err
	}

	var dataBytes []byte
	if raw, ok := res.Data.(json.RawMessage); ok {
		dataBytes = raw
	} else {
		dataBytes, _ = json.Marshal(res.Data)
	}
	reply := strings.Trim(string(dataBytes), "\"")

	if strings.HasPrefix(cmdStr, "SHELL ") {
		var shell struct {
			Stdout   string `json:"stdout"`
			Stderr   string `json:"stderr"`
			ExitCode int    `json:"exit_code"`
		}
		if json.Unmarshal(dataBytes, &shell) == nil {
			out := strings.TrimRight(shell.Stdout, "\n")
			if shell.Stderr != "" {
				out += "\n" + strings.TrimRight(shell.Stderr, "\n")
			}
			return out, nil, nil
		}
	}

	if strings.HasPrefix(cmdStr, "DOWNLOAD ") {
		// The first response is the size header; chunks follow on dlCh.
		var sizeHeader struct {
			Size  int    `json:"size"`
			Error string `json:"error"`
		}
		json.Unmarshal(dataBytes, &sizeHeader)
		if sizeHeader.Error != "" {
			s.dlMu.Lock()
			s.dlCh = nil
			s.dlMu.Unlock()
			return "ERR " + sizeHeader.Error, nil, nil
		}

		var buf []byte
		timeout := time.After(60 * time.Second)
		for done := false; !done; {
			select {
			case chunk, ok := <-dlCh:
				if !ok {
					done = true
				} else {
					buf = append(buf, chunk...)
					if len(buf) > 16<<20 {
						return "ERR download exceeds 16 MiB", nil, nil
					}
				}
			case <-timeout:
				return "ERR download timeout", nil, nil
			}
		}
		return fmt.Sprintf("received %d bytes", len(buf)), buf, nil
	}

	return reply, nil, nil
}

func (s *Server) callUpload(path string, data []byte) (string, error) {
	s.mu.Lock()
	connected := s.connected
	s.mu.Unlock()
	if !connected {
		return "", fmt.Errorf("agent is not connected")
	}

	id := nextCmdID()
	done := make(chan result, 1)
	uploadData, _ := json.Marshal(map[string]string{
		"path": path,
		"data": base64.StdEncoding.EncodeToString(data),
	})
	s.mu.Lock()
	s.queue = append(s.queue, pendingCmd{
		cmd:  command{ID: id, Op: "UPLOAD", Arg: path, Data: uploadData},
		done: done,
	})
	s.addLocked("out", fmt.Sprintf("UPLOAD %s (%d bytes)", path, len(data)))
	s.mu.Unlock()
	select {
	case s.notify <- struct{}{}:
	default:
	}

	select {
	case res := <-done:
		dataBytes, _ := json.Marshal(res.Data)
		return strings.Trim(string(dataBytes), "\""), nil
	case <-time.After(60 * time.Second):
		return "", fmt.Errorf("timeout")
	}
}

// --- Web UI handlers (relay from browser to agent) ---

func (s *Server) state(w http.ResponseWriter, _ *http.Request) {
	s.mu.Lock()
	defer s.mu.Unlock()
	writeJSON(w, map[string]any{
		"exercise":  s.cfg.ExerciseID,
		"title":     s.cfg.Title,
		"connected": s.connected,
		"verified":  s.seen,
		"peer":      s.agentID[:12],
		"protocol":  "AEAD",
		"cipher":    "AES-256-GCM",
		"agent_id":  s.agentID[:12],
		"events":    s.events,
		"commands":  s.cfg.Commands,
		"transfer":  s.cfg.Transfer,
	})
}

func (s *Server) webCommand(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "POST only", http.StatusMethodNotAllowed)
		return
	}
	var req struct {
		Command string `json:"command"`
	}
	if err := json.NewDecoder(io.LimitReader(r.Body, 1<<20)).Decode(&req); err != nil {
		http.Error(w, "invalid JSON", 400)
		return
	}
	cmd := strings.TrimSpace(req.Command)
	if !allowed(s.cfg.Commands, cmd) {
		writeJSON(w, map[string]any{"ok": false, "error": "command is not part of this exercise"})
		return
	}
	reply, data, err := s.call(cmd)
	if err != nil {
		writeJSON(w, map[string]any{"ok": false, "error": err.Error()})
		return
	}
	result := map[string]any{"ok": !strings.HasPrefix(reply, "ERR"), "reply": reply}
	if data != nil {
		result["data"] = base64.StdEncoding.EncodeToString(data)
		result["size"] = len(data)
	}
	writeJSON(w, result)
}

func (s *Server) upload(w http.ResponseWriter, r *http.Request) {
	if !s.cfg.Transfer {
		http.Error(w, "not available in this exercise", http.StatusNotFound)
		return
	}
	if r.Method != http.MethodPost {
		http.Error(w, "POST only", http.StatusMethodNotAllowed)
		return
	}
	if err := r.ParseMultipartForm(16 << 20); err != nil {
		http.Error(w, "invalid upload", http.StatusBadRequest)
		return
	}
	path := strings.TrimSpace(r.FormValue("path"))
	file, header, err := r.FormFile("file")
	if err != nil || path == "" {
		http.Error(w, "path and file are required", http.StatusBadRequest)
		return
	}
	defer file.Close()
	if header.Size > 16<<20 {
		http.Error(w, "file exceeds 16 MiB lab limit", http.StatusBadRequest)
		return
	}
	data, err := io.ReadAll(io.LimitReader(file, 16<<20))
	if err != nil {
		http.Error(w, err.Error(), 500)
		return
	}
	reply, err := s.callUpload(path, data)
	if err != nil {
		writeJSON(w, map[string]any{"ok": false, "error": err.Error()})
		return
	}
	writeJSON(w, map[string]any{"ok": !strings.HasPrefix(reply, "ERR"), "reply": reply, "size": len(data)})
}

func (s *Server) relayWait(w http.ResponseWriter, _ *http.Request) {
	s.mu.Lock()
	connected, seen := s.connected, s.seen
	s.mu.Unlock()
	if connected || (s.cfg.HandshakeOnly && seen) {
		io.WriteString(w, "CONNECTED\n")
		return
	}
	http.Error(w, "waiting for agent", http.StatusServiceUnavailable)
}

func (s *Server) relayConnections(w http.ResponseWriter, _ *http.Request) {
	s.mu.Lock()
	times := make([]int64, len(s.connTimes))
	copy(times, s.connTimes)
	s.mu.Unlock()
	for _, t := range times {
		fmt.Fprintln(w, t)
	}
}

func (s *Server) relayPTY(w http.ResponseWriter, _ *http.Request) {
	s.mu.Lock()
	out := s.ptyBuf
	s.ptyBuf = nil
	s.mu.Unlock()
	w.Write(out)
}

func (s *Server) relayCommand(w http.ResponseWriter, r *http.Request) {
	cmd := strings.TrimSpace(r.URL.Query().Get("q"))
	if !allowed(s.cfg.Commands, cmd) {
		http.Error(w, "command is not part of this exercise", http.StatusBadRequest)
		return
	}
	reply, _, err := s.call(cmd)
	if err != nil {
		http.Error(w, err.Error(), http.StatusServiceUnavailable)
		return
	}
	io.WriteString(w, reply+"\n")
}

func (s *Server) relayDownload(w http.ResponseWriter, r *http.Request) {
	data, ok := s.relayDownloadData(w, r)
	if ok {
		w.Write(data)
	}
}

func (s *Server) relayDownloadSize(w http.ResponseWriter, r *http.Request) {
	data, ok := s.relayDownloadData(w, r)
	if ok {
		fmt.Fprintf(w, "%d\n", len(data))
	}
}

func (s *Server) relayDownloadData(w http.ResponseWriter, r *http.Request) ([]byte, bool) {
	if !s.cfg.Transfer {
		http.Error(w, "not available in this exercise", http.StatusNotFound)
		return nil, false
	}
	path := strings.TrimSpace(r.URL.Query().Get("path"))
	if path == "" {
		http.Error(w, "path is required", http.StatusBadRequest)
		return nil, false
	}
	reply, data, err := s.call("DOWNLOAD " + path)
	if err != nil {
		http.Error(w, err.Error(), http.StatusServiceUnavailable)
		return nil, false
	}
	if strings.HasPrefix(reply, "ERR") {
		http.Error(w, reply, http.StatusBadRequest)
		return nil, false
	}
	return data, true
}

func (s *Server) relayUpload(w http.ResponseWriter, r *http.Request) {
	if !s.cfg.Transfer {
		http.Error(w, "not available in this exercise", http.StatusNotFound)
		return
	}
	if r.Method != http.MethodPost {
		http.Error(w, "POST only", http.StatusMethodNotAllowed)
		return
	}
	path := strings.TrimSpace(r.URL.Query().Get("path"))
	if path == "" {
		http.Error(w, "path is required", http.StatusBadRequest)
		return
	}
	data, err := io.ReadAll(io.LimitReader(r.Body, (16<<20)+1))
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	if len(data) > 16<<20 {
		http.Error(w, "file exceeds 16 MiB lab limit", http.StatusRequestEntityTooLarge)
		return
	}
	reply, err := s.callUpload(path, data)
	if err != nil {
		http.Error(w, err.Error(), http.StatusServiceUnavailable)
		return
	}
	io.WriteString(w, reply+"\n")
}

func allowed(commands []string, cmd string) bool {
	verb := strings.Fields(cmd)
	if len(verb) == 0 {
		return false
	}
	v := verb[0]
	for _, c := range commands {
		if strings.Fields(c)[0] == v {
			return true
		}
	}
	return false
}

func writeJSON(w http.ResponseWriter, v any) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(v)
}

func (s *Server) page(w http.ResponseWriter, _ *http.Request) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	fmt.Fprintf(w, labPage, s.cfg.Title, s.cfg.Code)
}

const labPage = `<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>%s</title><style>
:root{--bg:#f2eff8;--card:#fff;--ink:#1d1727;--muted:#746b82;--line:#ddd7e6;--main:#7047eb;--main-dark:#5530c9;--ok:#159447;--bad:#c63d45;--terminal:#17131e}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:14px ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;line-height:1.45}.wrap{max-width:1040px;margin:auto;padding:40px 28px 64px}header,.row,.status{display:flex;align-items:center}header{gap:14px;margin-bottom:26px}h1{margin:0;flex:1;font:750 23px system-ui,sans-serif;letter-spacing:-.02em}.tag{padding:7px 10px;border:2px solid var(--ink);background:var(--main);color:#fff;box-shadow:3px 3px 0 var(--ink);font-weight:900;letter-spacing:.04em}.status{gap:8px;padding:6px 10px;border:1px solid var(--line);background:#ffffffaa;font-size:12px;text-transform:uppercase;letter-spacing:.05em}.dot{width:8px;height:8px;border-radius:50%%;background:var(--bad);box-shadow:0 0 0 3px #c63d4518}.dot.on{background:var(--ok);box-shadow:0 0 0 3px #15944718}.card{overflow:hidden;margin-top:18px;border:1.5px solid var(--ink);background:var(--card);box-shadow:3px 3px 0 var(--ink)}.head{padding:10px 14px;border-bottom:1.5px solid var(--ink);background:#faf9fc;font:700 12px system-ui,sans-serif;text-transform:uppercase;letter-spacing:.08em}.body{padding:16px}.grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));padding:0}.cell{min-width:0;padding:14px 16px;border-right:1px solid var(--line)}.cell:last-child{border:0}.label,.section-label{color:var(--muted);font-size:10px;font-weight:800;text-transform:uppercase;letter-spacing:.08em}.value{margin-top:6px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:13px}.row{gap:10px}input{min-width:0;height:42px;padding:0 12px;border:1.5px solid var(--ink);outline:none;background:#fff;color:var(--ink);font:inherit;transition:border-color .12s,box-shadow .12s}input:focus{border-color:var(--main);box-shadow:0 0 0 3px #7047eb18}.command{flex:1}.path{width:40%%}.section-label{margin:18px 0 8px}.file-picker{display:flex;align-items:center;min-width:0;height:42px;flex:1;border:1.5px solid var(--ink);background:#fff;cursor:pointer}.file-picker:focus-within{border-color:var(--main);box-shadow:0 0 0 3px #7047eb18}.file-picker input{position:absolute;width:1px;height:1px;opacity:0;pointer-events:none}.browse{align-self:stretch;display:flex;align-items:center;padding:0 13px;border-right:1px solid var(--line);background:#f6f3fa;font-weight:800}.filename{min-width:0;overflow:hidden;padding:0 13px;color:var(--muted);text-overflow:ellipsis;white-space:nowrap}.file-picker.has-file .filename{color:var(--ink)}button{height:42px;padding:0 18px;border:1.5px solid var(--ink);background:var(--main);color:#fff;box-shadow:2px 2px 0 var(--ink);cursor:pointer;font-family:inherit;font-weight:800;transition:background .12s,transform .08s,box-shadow .08s}button:hover{background:var(--main-dark)}button:active{box-shadow:none;transform:translate(2px,2px)}#result{display:none;margin:14px 0 0;padding:11px 13px;border:1px solid var(--line);border-left:3px solid var(--main);background:#f8f6fc;white-space:pre-wrap;font-size:13px}#result.show{display:block}.log{height:250px;overflow:auto;padding:5px 16px 12px;background:var(--terminal);color:#e8e1f1}.line{display:grid;grid-template-columns:74px 18px 1fr;padding:8px 0;border-bottom:1px solid #ffffff10}.line:last-child{border:0}.time{color:#8e839b}.out{color:#a98aff}.in{color:#45d47e}a{color:var(--main);font-weight:800}@media(max-width:760px){.wrap{padding:24px 16px 48px}header{align-items:flex-start;flex-wrap:wrap}h1{order:3;flex-basis:100%%}.grid{grid-template-columns:repeat(2,minmax(0,1fr))}.cell:last-child{border:0}.cell:nth-child(2){border-right:0}.cell:nth-child(-n+2){border-bottom:1px solid var(--line)}}@media(max-width:560px){.row{align-items:stretch;flex-direction:column}.path{width:100%%}.file-picker{flex:none}.grid{grid-template-columns:1fr}.cell,.cell:nth-child(2){border:0;border-bottom:1px solid var(--line)}.cell:last-child{border:0}}</style></head><body><div class="wrap"><header><div class="tag">%s</div><h1 id="title">Rootkitlings Lab</h1><div class="status"><span class="dot" id="dot"></span><strong id="status">waiting</strong></div></header><section class="card"><div class="head">Connection</div><div class="grid"><div class="cell"><div class="label">Agent</div><div class="value" id="peer">--</div></div><div class="cell"><div class="label">Transport</div><div class="value" id="protocol">--</div></div><div class="cell"><div class="label">Cipher</div><div class="value" id="cipher">--</div></div><div class="cell"><div class="label">Key ID</div><div class="value" id="cert">--</div></div></div></section><section class="card" id="controls"><div class="head">Command</div><div class="body"><div class="row"><input class="command" id="command" list="suggestions" autocomplete="off" placeholder="PING, SHELL id, DOWNLOAD /etc/hostname"><datalist id="suggestions"></datalist><button id="send">Run</button></div><div id="upload" style="display:none"><div class="section-label">Upload to guest</div><div class="row"><input class="path" id="path" placeholder="Destination path"><label class="file-picker" id="picker"><input id="file" type="file"><span class="browse">Choose file</span><span class="filename" id="filename">No file selected</span></label><button id="put">Upload</button></div></div><pre id="result"></pre></div></section><section class="card"><div class="head">Protocol</div><div class="log" id="log"></div></section></div><script>
let state={};const e=id=>document.getElementById(id);function show(v){e('result').className='show';e('result').textContent=v}async function refresh(){const r=await fetch('/api/state');state=await r.json();e('title').textContent=state.title;e('dot').className='dot '+(state.connected||state.verified?'on':'');e('status').textContent=state.connected?'connected':state.verified?'verified':'waiting';e('peer').textContent=state.peer||'--';e('cert').textContent=state.cert_cn||'--';e('protocol').textContent=state.protocol||'--';e('cipher').textContent=state.cipher||'--';e('upload').style.display=state.transfer?'block':'none';const p=e('suggestions');if(!p.dataset.ready){(state.commands||[]).forEach(c=>p.append(new Option(c,c)));p.dataset.ready=1;if(!state.commands?.length)e('controls').style.display='none'}e('log').innerHTML=(state.events||[]).map(x=>'<div class="line"><span class="time">'+x.time+'</span><b class="'+x.direction+'">'+(x.direction==='out'?'->':'<-')+'</b> '+escapeHtml(x.message)+'</div>').join('');e('log').scrollTop=e('log').scrollHeight}function escapeHtml(s){const d=document.createElement('div');d.textContent=s;return d.innerHTML}async function run(){const command=e('command').value.trim();if(!command)return;const r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({command})});const d=await r.json();show(d.ok?(d.reply||('received '+d.size+' bytes')):(d.error||d.reply));if(d.data){const bytes=Uint8Array.from(atob(d.data),c=>c.charCodeAt(0));const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([bytes]));a.download=command.split('/').pop()||'download';a.textContent='Save '+a.download;e('result').append('\n',a)}refresh()}e('send').onclick=run;e('command').onkeydown=x=>{if(x.key==='Enter')run()};e('file').onchange=()=>{const file=e('file').files[0];e('filename').textContent=file?file.name:'No file selected';e('picker').classList.toggle('has-file',!!file)};e('put').onclick=async()=>{const file=e('file').files[0],path=e('path').value.trim();if(!file||!path){show('Choose a file and destination path.');return}const form=new FormData();form.append('path',path);form.append('file',file);const r=await fetch('/api/upload',{method:'POST',body:form});const d=await r.json();show(d.ok?d.reply+' · '+d.size+' bytes':d.error);refresh()};setInterval(refresh,1000);refresh();</script></body></html>`
