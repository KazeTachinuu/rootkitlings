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
	"os"

	"github.com/coder/websocket"
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
	// TODO 1: Derive the shared key.
	//   Parse agentPrivKey as an X25519 private key using crypto/ecdh.
	//   Parse operatorPubKey as an X25519 public key.
	//   Perform ECDH to get the shared secret, then derive a 32-byte key
	//   via HKDF with label "rklings-aead".
	//   Also derive the 32-byte agent public key (priv.PublicKey().Bytes())
	//   for the hello frame.

	// TODO 2: Build an AES-256-GCM cipher (cipher.AEAD) from the shared key.
	//   Use crypto/aes.NewCipher, then cipher.NewGCM.

	// TODO 3: Connect via WebSocket and send the hello frame.
	//   Dial ws://<serverAddr>/ using websocket.Dial(ctx, url, nil).
	//   Marshal {"t":"hello"} as JSON.
	//   Generate a random nonce (gcm.NonceSize() bytes from crypto/rand).
	//   Seal the JSON with GCM (nonce as both the nonce arg and prefix).
	//   The hello frame is: agentPubBytes(32 bytes) || ciphertext
	//   Write it with conn.Write(ctx, websocket.MessageBinary, frame).

	// TODO 4: Read one server message and decrypt it.
	//   Use conn.Read(ctx) to receive a binary frame.
	//   The first NonceSize bytes are the nonce, the rest is sealed data.
	//   Decrypt with gcm.Open.
	//   Print the decrypted JSON and exit.

	// Remove these lines after implementing:
	_ = ecdh.X25519()
	_ = sha256.Sum256(nil)
	_ = aes.NewCipher
	_ = cipher.NewGCM
	_ = rand.Read
	_ = json.Marshal
	_ = context.Background()
	_ = websocket.Dial
	return nil
}
