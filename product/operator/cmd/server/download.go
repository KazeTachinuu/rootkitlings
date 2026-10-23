package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"unicode"
)

func downloadToDisk(o *Operator, remotePath string) (string, error) {
	session, err := o.Agents.current()
	if err != nil {
		return "", err
	}
	data, err := o.SendDownload(session.id, remotePath)
	if err != nil {
		return "", err
	}
	destination, err := downloadPath(session.id, remotePath)
	if err != nil {
		return "", err
	}
	if err := writeAtomic(destination, data, 0o600); err != nil {
		return "", err
	}
	return fmt.Sprintf("saved %d bytes -> %s", len(data), destination), nil
}

func downloadPath(agentID, remotePath string) (string, error) {
	root := envOr("RKLINGS_DOWNLOAD_DIR", filepath.Join(".rklings", "downloads"))
	clean := filepath.Clean("/" + strings.TrimSpace(remotePath))
	relative := strings.TrimPrefix(clean, string(filepath.Separator))
	if relative == "" || relative == "." {
		return "", fmt.Errorf("invalid remote path")
	}
	return filepath.Join(root, safeFilename(agentID), relative), nil
}

func safeFilename(value string) string {
	value = strings.Map(func(r rune) rune {
		if unicode.IsLetter(r) || unicode.IsDigit(r) || r == '-' || r == '_' || r == '.' {
			return r
		}
		return '_'
	}, value)
	value = strings.Trim(value, ".")
	if value == "" {
		return "unknown"
	}
	return value
}

func writeAtomic(path string, data []byte, mode os.FileMode) error {
	directory := filepath.Dir(path)
	if err := os.MkdirAll(directory, 0o700); err != nil {
		return fmt.Errorf("create download directory: %w", err)
	}

	temporary, err := os.CreateTemp(directory, ".download-*")
	if err != nil {
		return fmt.Errorf("create temporary download: %w", err)
	}
	temporaryPath := temporary.Name()
	defer os.Remove(temporaryPath)

	if err := temporary.Chmod(mode); err != nil {
		temporary.Close()
		return fmt.Errorf("set download mode: %w", err)
	}
	if _, err := temporary.Write(data); err != nil {
		temporary.Close()
		return fmt.Errorf("write download: %w", err)
	}
	if err := temporary.Sync(); err != nil {
		temporary.Close()
		return fmt.Errorf("sync download: %w", err)
	}
	if err := temporary.Close(); err != nil {
		return fmt.Errorf("close download: %w", err)
	}
	if err := os.Rename(temporaryPath, path); err != nil {
		return fmt.Errorf("install download: %w", err)
	}
	return nil
}
