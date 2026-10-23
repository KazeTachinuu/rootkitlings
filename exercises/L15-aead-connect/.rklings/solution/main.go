// Agent AEAD connect.
// Connect to the rootless lesson server at 10.0.2.2:8443 over WebSocket.
// Embed the X25519 keys at compile time, derive a shared key, send an
// authenticated hello frame, and print the first decrypted server message.
package main

import (
	"context"
	"crypto/aes"
	"crypto/cipher"
	"crypto/ecdh"
	"crypto/rand"
	"crypto/sha256"
	_ "embed"
	"encoding/json"
	"fmt"
	"io"
	"os"

	"github.com/coder/websocket"
	"golang.org/x/crypto/hkdf"
)

//go:embed keys/agent.key
var agentPrivKey []byte

//go:embed keys/operator.pub
var operatorPubKey []byte

var serverAddr = envOr("RKLINGS_SERVER_ADDR", "10.0.2.2:8443")

func envOr(name, fallback string) string {
	if value := os.Getenv(name); value != "" {
		return value
	}
	return fallback
}

func main() {
	if err := run(); err != nil {
		fmt.Fprintf(os.Stderr, "agent: %v\n", err)
		os.Exit(1)
	}
}

func run() error {
	ctx := context.Background()

	// TODO 1: Derive the shared key.
	curve := ecdh.X25519()
	priv, err := curve.NewPrivateKey(agentPrivKey)
	if err != nil {
		return fmt.Errorf("parse agent private key: %w", err)
	}
	pub, err := curve.NewPublicKey(operatorPubKey)
	if err != nil {
		return fmt.Errorf("parse operator public key: %w", err)
	}
	agentPubBytes := priv.PublicKey().Bytes()
	secret, err := priv.ECDH(pub)
	if err != nil {
		return fmt.Errorf("ECDH: %w", err)
	}
	var sharedKey [32]byte
	if _, err := io.ReadFull(hkdf.New(sha256.New, secret, nil, []byte("rklings-aead")), sharedKey[:]); err != nil {
		return fmt.Errorf("derive key: %w", err)
	}

	// TODO 2: Build AES-256-GCM cipher.
	block, err := aes.NewCipher(sharedKey[:])
	if err != nil {
		return fmt.Errorf("aes: %w", err)
	}
	gcm, err := cipher.NewGCM(block)
	if err != nil {
		return fmt.Errorf("gcm: %w", err)
	}

	// TODO 3: Connect via WebSocket and send the hello frame.
	conn, _, err := websocket.Dial(ctx, fmt.Sprintf("ws://%s/", serverAddr), nil)
	if err != nil {
		return fmt.Errorf("dial: %w", err)
	}
	defer conn.CloseNow()
	conn.SetReadLimit(4 << 20)

	hello, _ := json.Marshal(map[string]string{"t": "hello"})
	nonce := make([]byte, gcm.NonceSize())
	if _, err := rand.Read(nonce); err != nil {
		return fmt.Errorf("nonce: %w", err)
	}
	ct := gcm.Seal(nonce, nonce, hello, nil)
	frame := append(agentPubBytes, ct...)
	if err := conn.Write(ctx, websocket.MessageBinary, frame); err != nil {
		return fmt.Errorf("write hello: %w", err)
	}

	// TODO 4: Read one server message and decrypt it.
	_, msg, err := conn.Read(ctx)
	if err != nil {
		return fmt.Errorf("read: %w", err)
	}
	if len(msg) < gcm.NonceSize() {
		return fmt.Errorf("message too short")
	}
	decrypted, err := gcm.Open(nil, msg[:gcm.NonceSize()], msg[gcm.NonceSize():], nil)
	if err != nil {
		return fmt.Errorf("decrypt: %w", err)
	}

	fmt.Println(string(decrypted))
	return nil
}
