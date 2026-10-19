package labserver

import (
	"bufio"
	"context"
	"crypto/tls"
	"crypto/x509"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"
)

type Config struct {
	ExerciseID    string
	Title         string
	CertFile      string
	KeyFile       string
	CAFile        string
	TLSAddr       string
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

type Server struct {
	cfg                            Config
	mu                             sync.Mutex
	io                             sync.Mutex
	conn                           net.Conn
	reader                         *bufio.Reader
	peer, protocol, cipher, certCN string
	connected                      bool
	seen                           bool
	events                         []Event
	connections                    []int64
	tlsListener                    net.Listener
	httpListener                   net.Listener
	httpServer                     *http.Server
}

func (s *Server) AgentAddr() string {
	_, port, err := net.SplitHostPort(s.tlsListener.Addr().String())
	if err != nil {
		return "10.0.2.2:8443"
	}
	return net.JoinHostPort("10.0.2.2", port)
}

func (s *Server) RelayURL() string {
	_, port, err := net.SplitHostPort(s.httpListener.Addr().String())
	if err != nil {
		return "http://10.0.2.2:4444"
	}
	return "http://" + net.JoinHostPort("10.0.2.2", port)
}

func Start(cfg Config) (*Server, error) {
	if cfg.TLSAddr == "" {
		cfg.TLSAddr = ":8443"
	}
	if cfg.HTTPAddr == "" {
		cfg.HTTPAddr = "127.0.0.1:8080"
	}
	cert, err := tls.LoadX509KeyPair(cfg.CertFile, cfg.KeyFile)
	if err != nil {
		return nil, fmt.Errorf("load server certificate: %w", err)
	}
	caPEM, err := os.ReadFile(cfg.CAFile)
	if err != nil {
		return nil, fmt.Errorf("read CA: %w", err)
	}
	pool := x509.NewCertPool()
	if !pool.AppendCertsFromPEM(caPEM) {
		return nil, fmt.Errorf("parse CA certificate")
	}
	ln, err := tls.Listen("tcp", cfg.TLSAddr, &tls.Config{
		Certificates: []tls.Certificate{cert}, ClientCAs: pool,
		ClientAuth: tls.RequireAndVerifyClientCert, MinVersion: tls.VersionTLS13,
	})
	if err != nil {
		return nil, fmt.Errorf("listen %s: %w", cfg.TLSAddr, err)
	}
	s := &Server{cfg: cfg, tlsListener: ln}
	mux := http.NewServeMux()
	mux.HandleFunc("/", s.page)
	mux.HandleFunc("/api/state", s.state)
	mux.HandleFunc("/api/command", s.command)
	mux.HandleFunc("/api/upload", s.upload)
	// Guest checks use this deliberately small relay API. Every endpoint below
	// forwards to the connected mTLS agent; none of them emulate agent output.
	mux.HandleFunc("/wait", s.relayWait)
	mux.HandleFunc("/cmd", s.relayCommand)
	mux.HandleFunc("/download", s.relayDownload)
	mux.HandleFunc("/download-size", s.relayDownloadSize)
	mux.HandleFunc("/upload", s.relayUpload)
	mux.HandleFunc("/connections", s.relayConnections)
	s.httpServer = &http.Server{Addr: cfg.HTTPAddr, Handler: mux, ReadHeaderTimeout: 5 * time.Second}
	httpLn, err := net.Listen("tcp", cfg.HTTPAddr)
	if err != nil {
		ln.Close()
		return nil, fmt.Errorf("listen %s: %w", cfg.HTTPAddr, err)
	}
	s.httpListener = httpLn
	go s.accept()
	go s.httpServer.Serve(httpLn)
	return s, nil
}

func (s *Server) Close() error {
	s.mu.Lock()
	if s.conn != nil {
		s.conn.Close()
	}
	s.mu.Unlock()
	_ = s.tlsListener.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	return s.httpServer.Shutdown(ctx)
}

func (s *Server) accept() {
	for {
		conn, err := s.tlsListener.Accept()
		if err != nil {
			return
		}
		tlsConn := conn.(*tls.Conn)
		if err := tlsConn.Handshake(); err != nil {
			conn.Close()
			continue
		}
		state := tlsConn.ConnectionState()
		cn := ""
		if len(state.PeerCertificates) > 0 {
			cn = state.PeerCertificates[0].Subject.CommonName
		}
		s.mu.Lock()
		if s.conn != nil {
			s.conn.Close()
		}
		s.conn, s.reader, s.connected, s.seen = conn, bufio.NewReader(conn), true, true
		s.peer, s.protocol, s.cipher, s.certCN = conn.RemoteAddr().String(), tls.VersionName(state.Version), tls.CipherSuiteName(state.CipherSuite), cn
		s.connections = append(s.connections, time.Now().UnixNano())
		s.addLocked("in", "agent connected: "+cn)
		s.mu.Unlock()
		if s.cfg.HandshakeOnly {
			s.io.Lock()
			_, _ = fmt.Fprintln(conn, s.cfg.Welcome)
			s.io.Unlock()
			s.mu.Lock()
			s.addLocked("out", "WELCOME rootkitlings")
			s.connected = false
			s.mu.Unlock()
			conn.Close()
		}
	}
}

func (s *Server) relayConnections(w http.ResponseWriter, _ *http.Request) {
	s.mu.Lock()
	defer s.mu.Unlock()
	for _, timestamp := range s.connections {
		_, _ = fmt.Fprintln(w, timestamp)
	}
}

func (s *Server) addLocked(direction, message string) {
	s.events = append(s.events, Event{Time: time.Now().Format("15:04:05"), Direction: direction, Message: message})
	if len(s.events) > 100 {
		s.events = s.events[len(s.events)-100:]
	}
}

func (s *Server) state(w http.ResponseWriter, _ *http.Request) {
	s.mu.Lock()
	defer s.mu.Unlock()
	writeJSON(w, map[string]any{"exercise": s.cfg.ExerciseID, "title": s.cfg.Title, "connected": s.connected, "verified": s.seen, "peer": s.peer, "protocol": s.protocol, "cipher": s.cipher, "cert_cn": s.certCN, "events": s.events, "commands": s.cfg.Commands, "transfer": s.cfg.Transfer})
}

func (s *Server) command(w http.ResponseWriter, r *http.Request) {
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
		_, _ = io.WriteString(w, "CONNECTED\n")
		return
	}
	http.Error(w, "waiting for agent", http.StatusServiceUnavailable)
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
	_, _ = io.WriteString(w, reply+"\n")
}

func (s *Server) relayDownload(w http.ResponseWriter, r *http.Request) {
	data, ok := s.relayDownloadData(w, r)
	if ok {
		_, _ = w.Write(data)
	}
}

func (s *Server) relayDownloadSize(w http.ResponseWriter, r *http.Request) {
	data, ok := s.relayDownloadData(w, r)
	if ok {
		_, _ = fmt.Fprintf(w, "%d\n", len(data))
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
	_, _ = io.WriteString(w, reply+"\n")
}

func (s *Server) callUpload(path string, data []byte) (string, error) {
	s.io.Lock()
	defer s.io.Unlock()
	s.mu.Lock()
	conn, reader, connected := s.conn, s.reader, s.connected
	s.mu.Unlock()
	if !connected || conn == nil {
		return "", fmt.Errorf("agent is not connected")
	}
	command := fmt.Sprintf("UPLOAD %s %d", path, len(data))
	if _, err := fmt.Fprintln(conn, command); err != nil {
		return "", err
	}
	if _, err := conn.Write(data); err != nil {
		return "", err
	}
	s.mu.Lock()
	s.addLocked("out", fmt.Sprintf("%s (%d bytes)", command, len(data)))
	s.mu.Unlock()
	line, err := reader.ReadString('\n')
	if err != nil {
		return "", err
	}
	reply := strings.TrimRight(line, "\r\n")
	s.mu.Lock()
	s.addLocked("in", reply)
	s.mu.Unlock()
	return reply, nil
}

func (s *Server) call(cmd string) (string, []byte, error) {
	s.io.Lock()
	defer s.io.Unlock()
	s.mu.Lock()
	conn, reader := s.conn, s.reader
	connected := s.connected
	s.mu.Unlock()
	if !connected || conn == nil {
		return "", nil, fmt.Errorf("agent is not connected")
	}
	if _, err := fmt.Fprintln(conn, cmd); err != nil {
		return "", nil, err
	}
	s.mu.Lock()
	s.addLocked("out", cmd)
	s.mu.Unlock()
	var reply string
	var data []byte
	if strings.HasPrefix(cmd, "SHELL ") {
		var lines []string
		for {
			line, err := reader.ReadString('\n')
			if err != nil {
				return "", nil, err
			}
			line = strings.TrimRight(line, "\r\n")
			if line == "." {
				break
			}
			lines = append(lines, line)
		}
		reply = strings.Join(lines, "\n")
	} else if strings.HasPrefix(cmd, "DOWNLOAD ") {
		line, err := reader.ReadString('\n')
		if err != nil {
			return "", nil, err
		}
		line = strings.TrimSpace(line)
		if strings.HasPrefix(line, "ERR") {
			reply = line
		} else {
			size, err := strconv.ParseInt(line, 10, 64)
			if err != nil {
				return "", nil, fmt.Errorf("invalid size header: %s", line)
			}
			data = make([]byte, size)
			if _, err = io.ReadFull(reader, data); err != nil {
				return "", nil, err
			}
			reply = fmt.Sprintf("received %d bytes", size)
		}
	} else {
		line, err := reader.ReadString('\n')
		if err != nil {
			return "", nil, err
		}
		reply = strings.TrimRight(line, "\r\n")
	}
	s.mu.Lock()
	s.addLocked("in", reply)
	s.mu.Unlock()
	return reply, data, nil
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
	_ = json.NewEncoder(w).Encode(v)
}

func (s *Server) page(w http.ResponseWriter, _ *http.Request) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	fmt.Fprintf(w, labPage, s.cfg.Title, s.cfg.Code)
}

const labPage = `<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>%s · Rootkitlings</title><style>
:root{--bg:#f2eff8;--card:#fff;--ink:#1d1727;--muted:#746b82;--line:#ddd7e6;--main:#7047eb;--main-dark:#5530c9;--ok:#159447;--bad:#c63d45;--terminal:#17131e}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:14px ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;line-height:1.45}.wrap{max-width:1040px;margin:auto;padding:40px 28px 64px}header,.row,.status{display:flex;align-items:center}header{gap:14px;margin-bottom:26px}h1{margin:0;flex:1;font:750 23px system-ui,sans-serif;letter-spacing:-.02em}.tag{padding:7px 10px;border:2px solid var(--ink);background:var(--main);color:#fff;box-shadow:3px 3px 0 var(--ink);font-weight:900;letter-spacing:.04em}.status{gap:8px;padding:6px 10px;border:1px solid var(--line);background:#ffffffaa;font-size:12px;text-transform:uppercase;letter-spacing:.05em}.dot{width:8px;height:8px;border-radius:50%%;background:var(--bad);box-shadow:0 0 0 3px #c63d4518}.dot.on{background:var(--ok);box-shadow:0 0 0 3px #15944718}.card{overflow:hidden;margin-top:18px;border:1.5px solid var(--ink);background:var(--card);box-shadow:3px 3px 0 var(--ink)}.head{padding:10px 14px;border-bottom:1.5px solid var(--ink);background:#faf9fc;font:700 12px system-ui,sans-serif;text-transform:uppercase;letter-spacing:.08em}.body{padding:16px}.grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));padding:0}.cell{min-width:0;padding:14px 16px;border-right:1px solid var(--line)}.cell:last-child{border:0}.label,.section-label{color:var(--muted);font-size:10px;font-weight:800;text-transform:uppercase;letter-spacing:.08em}.value{margin-top:6px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:13px}.row{gap:10px}input{min-width:0;height:42px;padding:0 12px;border:1.5px solid var(--ink);outline:none;background:#fff;color:var(--ink);font:inherit;transition:border-color .12s,box-shadow .12s}input:focus{border-color:var(--main);box-shadow:0 0 0 3px #7047eb18}.command{flex:1}.path{width:40%%}.section-label{margin:18px 0 8px}.file-picker{display:flex;align-items:center;min-width:0;height:42px;flex:1;border:1.5px solid var(--ink);background:#fff;cursor:pointer}.file-picker:focus-within{border-color:var(--main);box-shadow:0 0 0 3px #7047eb18}.file-picker input{position:absolute;width:1px;height:1px;opacity:0;pointer-events:none}.browse{align-self:stretch;display:flex;align-items:center;padding:0 13px;border-right:1px solid var(--line);background:#f6f3fa;font-weight:800}.filename{min-width:0;overflow:hidden;padding:0 13px;color:var(--muted);text-overflow:ellipsis;white-space:nowrap}.file-picker.has-file .filename{color:var(--ink)}button{height:42px;padding:0 18px;border:1.5px solid var(--ink);background:var(--main);color:#fff;box-shadow:2px 2px 0 var(--ink);cursor:pointer;font-family:inherit;font-weight:800;transition:background .12s,transform .08s,box-shadow .08s}button:hover{background:var(--main-dark)}button:active{box-shadow:none;transform:translate(2px,2px)}#result{display:none;margin:14px 0 0;padding:11px 13px;border:1px solid var(--line);border-left:3px solid var(--main);background:#f8f6fc;white-space:pre-wrap;font-size:13px}#result.show{display:block}.log{height:250px;overflow:auto;padding:5px 16px 12px;background:var(--terminal);color:#e8e1f1}.line{display:grid;grid-template-columns:74px 18px 1fr;padding:8px 0;border-bottom:1px solid #ffffff10}.line:last-child{border:0}.time{color:#8e839b}.out{color:#a98aff}.in{color:#45d47e}a{color:var(--main);font-weight:800}@media(max-width:760px){.wrap{padding:24px 16px 48px}header{align-items:flex-start;flex-wrap:wrap}h1{order:3;flex-basis:100%%}.grid{grid-template-columns:repeat(2,minmax(0,1fr))}.cell:nth-child(2){border-right:0}.cell:nth-child(-n+2){border-bottom:1px solid var(--line)}}@media(max-width:560px){.row{align-items:stretch;flex-direction:column}.path{width:100%%}.file-picker{flex:none}.grid{grid-template-columns:1fr}.cell,.cell:nth-child(2){border:0;border-bottom:1px solid var(--line)}.cell:last-child{border:0}}</style></head><body><div class="wrap"><header><div class="tag">%s</div><h1 id="title">Rootkitlings Lab</h1><div class="status"><span class="dot" id="dot"></span><strong id="status">waiting</strong></div></header><section class="card"><div class="head">Connection</div><div class="grid"><div class="cell"><div class="label">Peer</div><div class="value" id="peer">—</div></div><div class="cell"><div class="label">Certificate</div><div class="value" id="cert">—</div></div><div class="cell"><div class="label">Transport</div><div class="value" id="protocol">—</div></div><div class="cell"><div class="label">Cipher</div><div class="value" id="cipher">—</div></div></div></section><section class="card" id="controls"><div class="head">Command</div><div class="body"><div class="row"><input class="command" id="command" list="suggestions" autocomplete="off" placeholder="PING, SHELL id, DOWNLOAD /etc/hostname"><datalist id="suggestions"></datalist><button id="send">Run</button></div><div id="upload" style="display:none"><div class="section-label">Upload to guest</div><div class="row"><input class="path" id="path" placeholder="Destination path"><label class="file-picker" id="picker"><input id="file" type="file"><span class="browse">Choose file</span><span class="filename" id="filename">No file selected</span></label><button id="put">Upload</button></div></div><pre id="result"></pre></div></section><section class="card"><div class="head">Protocol</div><div class="log" id="log"></div></section></div><script>
let state={};const e=id=>document.getElementById(id);function show(v){e('result').className='show';e('result').textContent=v}async function refresh(){const r=await fetch('/api/state');state=await r.json();e('title').textContent=state.title;e('dot').className='dot '+(state.connected||state.verified?'on':'');e('status').textContent=state.connected?'connected':state.verified?'verified':'waiting';e('peer').textContent=state.peer||'—';e('cert').textContent=state.cert_cn||'—';e('protocol').textContent=state.protocol||'—';e('cipher').textContent=state.cipher||'—';e('upload').style.display=state.transfer?'block':'none';const p=e('suggestions');if(!p.dataset.ready){(state.commands||[]).forEach(c=>p.append(new Option(c,c)));p.dataset.ready=1;if(!state.commands?.length)e('controls').style.display='none'}e('log').innerHTML=(state.events||[]).map(x=>'<div class="line"><span class="time">'+x.time+'</span><b class="'+x.direction+'">'+(x.direction==='out'?'→':'←')+'</b> '+escapeHtml(x.message)+'</div>').join('');e('log').scrollTop=e('log').scrollHeight}function escapeHtml(s){const d=document.createElement('div');d.textContent=s;return d.innerHTML}async function run(){const command=e('command').value.trim();if(!command)return;const r=await fetch('/api/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({command})});const d=await r.json();show(d.ok?(d.reply||('received '+d.size+' bytes')):(d.error||d.reply));if(d.data){const bytes=Uint8Array.from(atob(d.data),c=>c.charCodeAt(0));const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([bytes]));a.download=command.split('/').pop()||'download';a.textContent='Save '+a.download;e('result').append('\n',a)}refresh()}e('send').onclick=run;e('command').onkeydown=x=>{if(x.key==='Enter')run()};e('file').onchange=()=>{const file=e('file').files[0];e('filename').textContent=file?file.name:'No file selected';e('picker').classList.toggle('has-file',!!file)};e('put').onclick=async()=>{const file=e('file').files[0],path=e('path').value.trim();if(!file||!path){show('Choose a file and destination path.');return}const form=new FormData();form.append('path',path);form.append('file',file);const r=await fetch('/api/upload',{method:'POST',body:form});const d=await r.json();show(d.ok?d.reply+' · '+d.size+' bytes':d.error);refresh()};setInterval(refresh,1000);refresh();</script></body></html>`
