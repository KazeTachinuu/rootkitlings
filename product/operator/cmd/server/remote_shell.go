package main

import (
	"encoding/base64"
	"fmt"
	"io"
	"os"
	"strings"

	"golang.org/x/term"
)

type remoteShell struct {
	op      *Operator
	agentID string
	stdin   io.Reader
	stdout  io.Writer
	stderr  io.Writer
}

func (s *remoteShell) SetStdin(r io.Reader)  { s.stdin = r }
func (s *remoteShell) SetStdout(w io.Writer) { s.stdout = w }
func (s *remoteShell) SetStderr(w io.Writer) { s.stderr = w }

func (s *remoteShell) Run() error {
	session, err := s.op.Agents.current()
	if err != nil {
		return err
	}
	s.agentID = session.id

	fd := int(os.Stdin.Fd())
	state, err := term.MakeRaw(fd)
	if err != nil {
		return fmt.Errorf("raw terminal: %w", err)
	}
	defer term.Restore(fd, state)
	defer s.op.SendCommand(s.agentID, "PTY", "CLOSE")

	raw, err := s.op.SendCommand(s.agentID, "PTY", "START")
	if err != nil {
		return err
	}
	if strings.HasPrefix(rawToString(raw), "ERR") {
		return fmt.Errorf("%s", rawToString(raw))
	}
	if cols, rows, sizeErr := term.GetSize(fd); sizeErr == nil {
		s.op.SendCommand(s.agentID, "PTY", fmt.Sprintf("RESIZE %d %d", cols, rows))
	}
	_, _ = io.WriteString(s.stdout, "\r\n[interactive shell; Ctrl+] to return]\r\n")

	ptyCh := make(chan []byte, 64)
	session.ptyMu.Lock()
	session.ptySub = ptyCh
	session.ptyMu.Unlock()
	defer func() {
		session.ptyMu.Lock()
		session.ptySub = nil
		session.ptyMu.Unlock()
	}()

	go func() {
		for data := range ptyCh {
			s.stdout.Write(data)
		}
	}()

	buf := make([]byte, 1024)
	for {
		n, err := s.stdin.Read(buf)
		if err != nil {
			return err
		}
		data := buf[:n]
		for i, b := range data {
			if b == 0x1d {
				if i > 0 {
					s.op.SendCommand(s.agentID, "PTY", "INPUT "+base64.StdEncoding.EncodeToString(data[:i]))
				}
				return nil
			}
		}
		s.op.SendCommand(s.agentID, "PTY", "INPUT "+base64.StdEncoding.EncodeToString(data))
	}
}
