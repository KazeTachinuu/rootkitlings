package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestDownloadPathPreservesSafeRemoteHierarchy(t *testing.T) {
	t.Setenv("RKLINGS_DOWNLOAD_DIR", "/tmp/downloads")
	got, err := downloadPath("victim:one", "/etc/ssh/sshd_config")
	if err != nil {
		t.Fatal(err)
	}
	want := filepath.Join("/tmp/downloads", "victim_one", "etc", "ssh", "sshd_config")
	if got != want {
		t.Fatalf("downloadPath() = %q, want %q", got, want)
	}
}

func TestDownloadPathCannotEscapeRoot(t *testing.T) {
	root := t.TempDir()
	t.Setenv("RKLINGS_DOWNLOAD_DIR", root)
	got, err := downloadPath("victim", "../../etc/shadow")
	if err != nil {
		t.Fatal(err)
	}
	if filepath.Dir(got) != filepath.Join(root, "victim", "etc") {
		t.Fatalf("download escaped root: %q", got)
	}
}

func TestWriteAtomicPreservesBinaryData(t *testing.T) {
	path := filepath.Join(t.TempDir(), "nested", "payload.bin")
	want := []byte{0x00, 0xff, 0x7f, '\n'}
	if err := writeAtomic(path, want, 0o600); err != nil {
		t.Fatal(err)
	}
	got, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if string(got) != string(want) {
		t.Fatalf("download bytes = %v, want %v", got, want)
	}
	info, err := os.Stat(path)
	if err != nil {
		t.Fatal(err)
	}
	if info.Mode().Perm() != 0o600 {
		t.Fatalf("download mode = %o, want 600", info.Mode().Perm())
	}
}
