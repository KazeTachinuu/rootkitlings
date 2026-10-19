package main

import (
	"encoding/base64"
	"fmt"
	"io"
	"os"
	"strings"

	"golang.org/x/sys/unix"
	"golang.org/x/term"
)

type remoteShell struct {
	stdin  io.Reader
	stdout io.Writer
	stderr io.Writer
}

func (s *remoteShell) SetStdin(r io.Reader)  { s.stdin = r }
func (s *remoteShell) SetStdout(w io.Writer) { s.stdout = w }
func (s *remoteShell) SetStderr(w io.Writer) { s.stderr = w }

func (s *remoteShell) Run() error {
	fd := int(os.Stdin.Fd())
	state, err := term.MakeRaw(fd)
	if err != nil {
		return fmt.Errorf("raw terminal: %w", err)
	}
	defer term.Restore(fd, state)
	defer sendToAgent("PTY CLOSE")

	if err := s.exchange("PTY START"); err != nil {
		return err
	}
	if cols, rows, sizeErr := term.GetSize(fd); sizeErr == nil {
		if err := s.exchange(fmt.Sprintf("PTY RESIZE %d %d", cols, rows)); err != nil {
			return err
		}
	}
	_, _ = io.WriteString(s.stdout, "\r\n[interactive shell; Ctrl+] to return]\r\n")

	buf := make([]byte, 1024)
	poll := []unix.PollFd{{Fd: int32(fd), Events: unix.POLLIN}}
	for {
		ready, err := unix.Poll(poll, 100)
		if err != nil && err != unix.EINTR {
			return fmt.Errorf("poll terminal: %w", err)
		}
		if ready > 0 && poll[0].Revents&unix.POLLIN != 0 {
			n, readErr := s.stdin.Read(buf)
			if readErr != nil {
				return readErr
			}
			data := buf[:n]
			for i, b := range data {
				if b == 0x1d {
					if i > 0 {
						_ = s.input(data[:i])
					}
					return nil
				}
			}
			if err := s.input(data); err != nil {
				return err
			}
		} else {
			if err := s.exchange("PTY POLL"); err != nil {
				return err
			}
		}
	}
}

func (s *remoteShell) input(data []byte) error {
	return s.exchange("PTY INPUT " + base64.StdEncoding.EncodeToString(data))
}

func (s *remoteShell) exchange(command string) error {
	reply, err := sendToAgent(command)
	if err != nil {
		return err
	}
	if strings.HasPrefix(reply, "ERR") {
		return fmt.Errorf("%s", strings.TrimSpace(strings.TrimPrefix(reply, "ERR")))
	}
	payload := strings.TrimSpace(strings.TrimPrefix(reply, "DATA"))
	if payload == "" {
		return nil
	}
	data, err := base64.StdEncoding.DecodeString(payload)
	if err != nil {
		return fmt.Errorf("decode PTY output: %w", err)
	}
	_, err = s.stdout.Write(data)
	return err
}
