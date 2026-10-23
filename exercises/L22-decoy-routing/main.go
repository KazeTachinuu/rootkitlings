package main

import (
	"context"
	"crypto/aes"
	"crypto/cipher"
	"crypto/ecdh"
	"crypto/rand"
	"crypto/sha256"
	_ "embed"
	"encoding/hex"
	"io"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net/http"
	"os"
	"sync"
	"time"

	"github.com/coder/websocket"
	"golang.org/x/crypto/hkdf"
)

// operatorKey holds the operator's X25519 private key.
// In production, this is embedded at build time.
// For the exercise, the check harness provides it via RKLINGS_OPERATOR_KEY.
var operatorKey []byte

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

// Session registry: caches derived keys for connected agents.
type SessionRegistry struct {
	mu   sync.RWMutex
	keys map[string][32]byte
}

func (sr *SessionRegistry) Lookup(pubHex string) ([32]byte, bool) {
	sr.mu.RLock()
	k, ok := sr.keys[pubHex]
	sr.mu.RUnlock()
	return k, ok
}

func (sr *SessionRegistry) Register(pubHex string, key [32]byte) {
	sr.mu.Lock()
	sr.keys[pubHex] = key
	sr.mu.Unlock()
}

// --- Crypto primitives (provided) ---

var errCiphertextShort = errors.New("ciphertext too short")

func newGCM(key [32]byte) (cipher.AEAD, error) {
	block, err := aes.NewCipher(key[:])
	if err != nil {
		return nil, err
	}
	return cipher.NewGCM(block)
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

// --- Decoy page ---

const decoyHTML = `<!DOCTYPE html>
<html><head><title>EduSigh</title></head>
<body><h1>Attendance, powered by one heroic administrator.</h1>
<p>Pedro is resending your signature email. Please keep your QR code ready.</p></body>
</html>`

func serveDecoy(w http.ResponseWriter) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	fmt.Fprint(w, decoyHTML)
}

func main() {
	sessions := &SessionRegistry{keys: make(map[string][32]byte)}

	// Load the operator private key from the environment (hex-encoded, 32 bytes).
	// The check harness sets RKLINGS_OPERATOR_KEY.
	if envKey := os.Getenv("RKLINGS_OPERATOR_KEY"); envKey != "" {
		b, err := hex.DecodeString(envKey)
		if err == nil && len(b) == 32 {
			operatorKey = b
			log.Println("operator key loaded from environment")
		}
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		// Try to accept a WebSocket upgrade. If it fails, serve the decoy.
		conn, err := websocket.Accept(w, r, nil)
		if err != nil {
			serveDecoy(w)
			return
		}
		defer conn.CloseNow()
		ctx := context.Background()

		_ = sessions    // remove this line
		_ = operatorKey // remove this line

		// TODO 1: Read the first WebSocket message (the hello frame).
		//   - The frame is: agent_pub (32 bytes) || ciphertext.
		//   - You need at least 32 + 12 + 16 bytes (pubkey + nonce + GCM tag).
		//   - Extract the 32-byte public key, derive the shared key with deriveKeys.
		//   - If anything fails, return (deferred CloseNow handles cleanup).

		// TODO 2: Try to decrypt with openAEAD.
		//   - If decryption fails, return.
		//   - If it succeeds, this is an authenticated agent.
		//   - Enter a read loop: for each message, decrypt, unmarshal as a Command,
		//     build a PING response, seal with sealAEAD, and write back.

		_ = ctx
	})

	addr := listenAddr()
	log.Printf("listening on %s", addr)
	log.Fatal(http.ListenAndServe(addr, mux))
}

func listenAddr() string {
	if addr := os.Getenv("RKLINGS_LISTEN_ADDR"); addr != "" {
		return addr
	}
	return ":8080"
}

// Helpers the solution will use (suppress unused-import errors).
var _ = json.Marshal
var _ = time.Second
var _ = hex.EncodeToString
