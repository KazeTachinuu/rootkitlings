// Package project resolves repository-local runtime paths.
package project

import (
	"fmt"
	"os"
	"path/filepath"
)

// Root returns the nearest ancestor containing course.toml.
func Root() (string, error) {
	dir, err := os.Getwd()
	if err != nil {
		return "", fmt.Errorf("working directory: %w", err)
	}
	for {
		if _, err := os.Stat(filepath.Join(dir, "course.toml")); err == nil {
			return dir, nil
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			return "", fmt.Errorf("course.toml not found from %s", dir)
		}
		dir = parent
	}
}

// DataDir returns the ignored repository-local runtime directory.
func DataDir() (string, error) {
	if override := os.Getenv("RKLINGS_DATA_DIR"); override != "" {
		return filepath.Abs(override)
	}
	root, err := Root()
	if err != nil {
		return "", err
	}
	return filepath.Join(root, ".rklings"), nil
}

// TempDir creates an ephemeral directory inside the repository data directory.
func TempDir(pattern string) (string, error) {
	dataDir, err := DataDir()
	if err != nil {
		return "", err
	}
	parent := filepath.Join(dataDir, "tmp")
	if err := os.MkdirAll(parent, 0700); err != nil {
		return "", fmt.Errorf("create project temp directory: %w", err)
	}
	return os.MkdirTemp(parent, pattern)
}
